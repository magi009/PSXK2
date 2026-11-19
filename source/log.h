#pragma once
#include <cstdarg>

namespace logx {
void init(const char* path);
void shutdown();
void write(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void vwrite(const char* fmt, va_list ap);
}
#define LOGI(...) logx::write(__VA_ARGS__)
