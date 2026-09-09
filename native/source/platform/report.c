#include <dolphin/os.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void OSReport(char* format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

void __assert(char* file, u32 line, char* condition)
{
    OSPanic(file, (int) line, "assertion failed: %s", condition);
}

void OSPanic(char* file, int line, char* format, ...)
{
    va_list args;
    fprintf(stderr, "%s:%d: ", file, line);
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    abort();
}
