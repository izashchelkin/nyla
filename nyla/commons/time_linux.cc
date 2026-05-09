#include "nyla/commons/time.h"

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

} // namespace nyla
