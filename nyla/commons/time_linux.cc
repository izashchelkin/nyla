#include "nyla/commons/time.h"

#include <ctime>

#include "nyla/commons/platform_linux.h"

namespace nyla
{

auto API GetMonotonicTimeMillis() -> uint64_t
{
    timespec ts{};
    ASSERT(clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == 0);
    return ts.tv_sec * 1'000 + ts.tv_nsec / 1'000'000;
}

auto API GetMonotonicTimeMicros() -> uint64_t
{
    timespec ts{};
    ASSERT(clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == 0);
    return ts.tv_sec * 1'000'000 + ts.tv_nsec / 1'000;
}

auto API GetMonotonicTimeNanos() -> uint64_t
{
    timespec ts{};
    ASSERT(clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == 0);
    return ts.tv_sec * 1'000'000'000 + ts.tv_nsec;
}

// ─── Wall clock ───

auto API GetWallClockTime() -> wall_clock_time
{
    timespec ts{};
    ASSERT(clock_gettime(CLOCK_REALTIME, &ts) == 0);

    time_t t = (time_t)ts.tv_sec;
    tm local_tm{};
    localtime_r(&t, &local_tm);

    return {
        .year = local_tm.tm_year + 1900,
        .month = local_tm.tm_mon + 1,
        .day = local_tm.tm_mday,
        .hour = local_tm.tm_hour,
        .minute = local_tm.tm_min,
        .second = local_tm.tm_sec,
    };
}

} // namespace nyla
