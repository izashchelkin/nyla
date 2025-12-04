#include "nyla/commons/os/clock.h"

#include <cstdint>
#include <ctime>

#include "absl/log/check.h"

namespace nyla
{

uint64_t GetMonotonicTimeMillis()
{
    timespec ts{};
    CHECK_EQ(clock_gettime(CLOCK_MONOTONIC_RAW, &ts), 0);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

uint64_t GetMonotonicTimeMicros()
{
    timespec ts{};
    CHECK_EQ(clock_gettime(CLOCK_MONOTONIC_RAW, &ts), 0);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

uint64_t GetMonotonicTimeNanos()
{
    timespec ts{};
    CHECK_EQ(clock_gettime(CLOCK_MONOTONIC_RAW, &ts), 0);
    return ts.tv_sec * 1e9 + ts.tv_nsec;
}

} // namespace nyla