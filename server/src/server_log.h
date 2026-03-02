#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstring>

void server_log(bool is_error, const char* fmt, ...);