#pragma once

/// Load symbols from /proc/self/exe and install signal handlers for
/// SIGILL, SIGSEGV, SIGABRT, SIGBUS, SIGFPE.
/// On crash, prints a backtrace with resolved function names to stderr.
/// Addresses can also be resolved offline with:
///   addr2line -e <unstripped_binary> -f -C <addr>
void install_crash_handlers();
