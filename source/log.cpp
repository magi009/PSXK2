#include "log.h"
#include <cstdio>
#include <ctime>

static FILE* s_log = nullptr;

void logx::init(const char* path) { s_log = fopen(path, "w"); }
void logx::shutdown() { if (s_log) fclose(s_log); s_log = nullptr; }

void logx::vwrite(const char* fmt, va_list ap) {
    if (!s_log) return;
    vfprintf(s_log, fmt, ap);
    fputc('\n', s_log);
    fflush(s_log);
}

void logx::write(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vwrite(fmt, ap); va_end(ap);
}
