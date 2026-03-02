#include <cstdarg>
#include <cstdio>
#include <cstring>

void server_log(bool is_error, const char* fmt, ...)
{
    FILE* out = is_error ? stderr : stdout;
    va_list args;
    va_start(args, fmt);
    vfprintf(out, fmt, args);
    va_end(args);
    fflush(out);
}