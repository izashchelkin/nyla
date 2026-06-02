#include "nyla/commons/time.h"

#include "nyla/commons/headers_windows.h" // IWYU pragma: keep
#include "nyla/commons/intrin.h"

namespace nyla
{

namespace
{

auto GetPerformanceFreq() -> uint64_t
{
    static uint64_t cached = 0;
    if (!cached)
    {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        cached = (uint64_t)freq.QuadPart;
    }
    return cached;
}

auto GetPerformanceTicks() -> uint64_t
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (uint64_t)counter.QuadPart;
}

auto TicksTo(uint64_t ticks, uint64_t scale) -> uint64_t
{
    uint64_t hi;
    uint64_t lo = UMul128(ticks, scale, hi);
    uint64_t rem;
    return UDiv128(hi, lo, GetPerformanceFreq(), rem);
}

} // namespace

auto API GetMonotonicTimeMillis() -> uint64_t
{
    return TicksTo(GetPerformanceTicks(), 1'000ULL);
}

auto API GetMonotonicTimeMicros() -> uint64_t
{
    return TicksTo(GetPerformanceTicks(), 1'000'000ULL);
}

auto API GetMonotonicTimeNanos() -> uint64_t
{
    return TicksTo(GetPerformanceTicks(), 1'000'000'000ULL);
}

auto API GetWallClockTime() -> wall_clock_time
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    return {
        .year = (int)st.wYear,
        .month = (int)st.wMonth,
        .day = (int)st.wDay,
        .hour = (int)st.wHour,
        .minute = (int)st.wMinute,
        .second = (int)st.wSecond,
    };
}

} // namespace nyla