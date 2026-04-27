#include "src/utils/time.h"
#ifdef _WIN32
#    include <windows.h>
#    include <sys/timeb.h>
#else
#    include <time.h>
#endif

#ifdef _WIN32
static LARGE_INTEGER g_start_time;
static double g_time_scale;

void connx_time_init() {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&g_start_time);
    g_time_scale = 1.0 / (double)frequency.QuadPart;
}

connx_timespec connx_now(connx_clock_t type) {
    connx_timespec now_tv;
    LARGE_INTEGER timestamp;
    LONGLONG diff;
    struct _timeb now_tb;
    double now_dbl;
    if (type == CONNX_CLOCK_MONOTONIC) {
        QueryPerformanceCounter(&timestamp);
        diff = timestamp.QuadPart - g_start_time.QuadPart;
        now_dbl = (double)diff * g_time_scale;
        now_tv.tv_sec = (int64_t)now_dbl;
        now_tv.tv_nsec = (int64_t)((now_dbl - (double)now_tv.tv_sec) * 1e9);
    } else {
        _ftime_s(&now_tb);
        now_tv.tv_sec = (int64_t)now_tb.time;
        now_tv.tv_nsec = (int64_t)now_tb.millitm * 1000000;
    }
    return now_tv;
}
#else
void connx_time_init() {}
connx_timespec connx_now(connx_clock_t type) {
    struct timespec now;
    if (type == CONNX_CLOCK_MONOTONIC) {
        clock_gettime(CLOCK_MONOTONIC, &now);
    } else {
        clock_gettime(CLOCK_REALTIME, &now);
    }
    connx_timespec ts;
    ts.tv_sec = now.tv_sec;
    ts.tv_nsec = now.tv_nsec;
    return ts;
}

#endif

namespace connx {
int64_t GetCurrentMillisec() {
    auto ts = connx_now(CONNX_CLOCK_REALTIME);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
} // namespace connx
