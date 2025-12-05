#include "nyla/commons/os/clock.h"

#include <cstdint>
#include <ctime>

#include "absl/log/check.h"

namespace nyla
{

auto GetMonotonicTimeMillis() -> uint64_t
{
    timespec ts{};
    CHECK_EQ(clock_gettime(CLOCK_MONOTONIC_RAW, &ts), 0);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

auto GetMonotonicTimeMicros() -> uint64_t
{
    timespec ts{};
    CHECK_EQ(clock_gettime(CLOCK_MONOTONIC_RAW, &ts), 0);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

auto GetMonotonicTimeNanos() -> uint64_t
{
    timespec ts{};
    CHECK_EQ(clock_gettime(CLOCK_MONOTONIC_RAW, &ts), 0);
    return ts.tv_sec * 1e9 + ts.tv_nsec;
}

} // namespace nyla