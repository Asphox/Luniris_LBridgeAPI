#include "crash_handler.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>

#include <elf.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unwind.h>
#include <cxxabi.h>

// =============================================================================
// Symbol table (loaded once at startup, read-only after)
// =============================================================================

struct Symbol
{
    uintptr_t addr;
    const char* name; // points into mmap'd ELF or malloc'd demangled string
};

static std::vector<Symbol> g_symbols;
static void* g_elf_map = nullptr;
static size_t g_elf_size = 0;

static bool load_symbols()
{
    int fd = open("/proc/self/exe", O_RDONLY);
    if (fd < 0)
        return false;

    struct stat st;
    if (fstat(fd, &st) < 0)
    {
        close(fd);
        return false;
    }

    g_elf_size = static_cast<size_t>(st.st_size);
    g_elf_map = mmap(nullptr, g_elf_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    if (g_elf_map == MAP_FAILED)
    {
        g_elf_map = nullptr;
        return false;
    }

    auto* ehdr = static_cast<const Elf64_Ehdr*>(g_elf_map);

    // Validate ELF
    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0)
        return false;
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64)
        return false;

    auto* base = static_cast<const uint8_t*>(g_elf_map);
    auto* shdr = reinterpret_cast<const Elf64_Shdr*>(base + ehdr->e_shoff);

    // Find .symtab section
    for (uint16_t i = 0; i < ehdr->e_shnum; ++i)
    {
        if (shdr[i].sh_type != SHT_SYMTAB)
            continue;

        auto* syms = reinterpret_cast<const Elf64_Sym*>(base + shdr[i].sh_offset);
        auto* strtab = reinterpret_cast<const char*>(base + shdr[shdr[i].sh_link].sh_offset);
        size_t count = shdr[i].sh_size / sizeof(Elf64_Sym);

        g_symbols.reserve(count);
        for (size_t j = 0; j < count; ++j)
        {
            if (ELF64_ST_TYPE(syms[j].st_info) != STT_FUNC)
                continue;
            if (syms[j].st_value == 0)
                continue;

            const char* raw_name = strtab + syms[j].st_name;

            // Attempt C++ demangling
            int status;
            char* demangled = abi::__cxa_demangle(raw_name, nullptr, nullptr, &status);

            g_symbols.push_back({
                syms[j].st_value,
                (status == 0 && demangled) ? demangled : raw_name
            });
            // Note: demangled strings are intentionally not freed —
            // they must remain valid for the crash handler.
        }
        break; // only process first .symtab
    }

    std::sort(g_symbols.begin(), g_symbols.end(),
        [](const Symbol& a, const Symbol& b) { return a.addr < b.addr; });

    return !g_symbols.empty();
}

/// Find the nearest function symbol at or before `addr`.
/// Returns the symbol name, or nullptr if not found.
/// `offset` receives the byte offset from the symbol start.
static const char* resolve(uintptr_t addr, uintptr_t* offset)
{
    if (g_symbols.empty())
        return nullptr;

    // Binary search: find first symbol with addr > target, then step back
    auto it = std::upper_bound(g_symbols.begin(), g_symbols.end(), addr,
        [](uintptr_t a, const Symbol& s) { return a < s.addr; });

    if (it == g_symbols.begin())
        return nullptr;

    --it;
    *offset = addr - it->addr;
    return it->name;
}

// =============================================================================
// Backtrace via GCC _Unwind_Backtrace (works with musl)
// =============================================================================

struct BacktraceState
{
    void** current;
    void** end;
};

static _Unwind_Reason_Code unwind_callback(struct _Unwind_Context* ctx, void* arg)
{
    auto* state = static_cast<BacktraceState*>(arg);
    void* pc = reinterpret_cast<void*>(_Unwind_GetIP(ctx));
    if (pc)
    {
        if (state->current == state->end)
            return _URC_END_OF_STACK;
        *state->current++ = pc;
    }
    return _URC_NO_REASON;
}

// =============================================================================
// Signal handler
// =============================================================================

static const char* signal_name(int sig)
{
    switch (sig)
    {
        case SIGILL:  return "SIGILL";
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGBUS:  return "SIGBUS";
        case SIGFPE:  return "SIGFPE";
        default:      return "UNKNOWN";
    }
}

/// Dump the faulting instruction and surrounding context (aarch64 only).
/// For SIGILL / SIGFPE, si_addr points to the faulting instruction.
/// aarch64 instructions are always 4 bytes (A64 state).
static void dump_instruction_context(int sig, const void* fault_addr)
{
    // Only meaningful for signals where si_addr is an instruction pointer
    if (sig != SIGILL && sig != SIGFPE)
        return;
    if (!fault_addr)
        return;

    char buf[128];
    int len = snprintf(buf, sizeof(buf), "Instructions around faulting PC:\n");
    write(STDERR_FILENO, buf, len);

    auto* pc = reinterpret_cast<const uint32_t*>(fault_addr);

    // 3 instructions before, faulting instruction, 3 after
    for (int i = -3; i <= 3; ++i)
    {
        const uint32_t* addr = pc + i;
        const char* marker = (i == 0) ? "  <-- fault" : "";
        len = snprintf(buf, sizeof(buf), "  %p:  %08x%s\n",
            static_cast<const void*>(addr), *addr, marker);
        write(STDERR_FILENO, buf, len);
    }
}

static void crash_handler(int sig, siginfo_t* info, void* /*ucontext*/)
{
    // Note: snprintf is not strictly async-signal-safe, but acceptable
    // here since we _exit() immediately after and don't hold any locks.
    char buf[512];

    // --- Header with faulting address ---
    uintptr_t fault_addr = reinterpret_cast<uintptr_t>(info->si_addr);
    uintptr_t offset = 0;
    const char* fault_sym = resolve(fault_addr, &offset);

    int len;
    if (fault_sym)
    {
        len = snprintf(buf, sizeof(buf),
            "\n=== CRASH: %s at %p (%s+0x%lx) ===\n",
            signal_name(sig), info->si_addr, fault_sym, (unsigned long)offset);
    }
    else
    {
        len = snprintf(buf, sizeof(buf),
            "\n=== CRASH: %s at %p ===\n",
            signal_name(sig), info->si_addr);
    }
    write(STDERR_FILENO, buf, len);

    // --- Faulting instruction context ---
    dump_instruction_context(sig, info->si_addr);

    // --- Backtrace ---
    len = snprintf(buf, sizeof(buf), "Backtrace:\n");
    write(STDERR_FILENO, buf, len);

    void* frames[32];
    BacktraceState state = { frames, frames + 32 };
    _Unwind_Backtrace(unwind_callback, &state);

    int count = static_cast<int>(state.current - frames);
    for (int i = 0; i < count; ++i)
    {
        uintptr_t addr = reinterpret_cast<uintptr_t>(frames[i]);
        const char* name = resolve(addr, &offset);

        if (name)
        {
            len = snprintf(buf, sizeof(buf),
                "  #%-2d %p  %s+0x%lx\n",
                i, frames[i], name, (unsigned long)offset);
        }
        else
        {
            len = snprintf(buf, sizeof(buf),
                "  #%-2d %p\n", i, frames[i]);
        }
        write(STDERR_FILENO, buf, len);
    }

    const char footer[] = "=== END CRASH ===\n";
    write(STDERR_FILENO, footer, sizeof(footer) - 1);

    _exit(128 + sig);
}

// =============================================================================
// Public API
// =============================================================================

void install_crash_handlers()
{
    load_symbols();

    struct sigaction sa = {};
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGILL,  &sa, nullptr);
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
    sigaction(SIGFPE,  &sa, nullptr);
}
