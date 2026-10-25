#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0600
#endif
#include "runtime_clock.h"

#ifdef _WIN32
#include <windows.h>
double runtime_clock_now(void)
{
    LARGE_INTEGER counter, frequency;
    if (QueryPerformanceCounter(&counter) && QueryPerformanceFrequency(&frequency) &&
        frequency.QuadPart > 0)
        return (double)counter.QuadPart / (double)frequency.QuadPart;
    return (double)GetTickCount64() / 1000.0;
}
#else
#include <time.h>
double runtime_clock_now(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0.0;
    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}
#endif
