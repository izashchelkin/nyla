#include "nyla/commons/os/clock.h"
#include "nyla/commons/assert.h"

#include <cstdint>

#if defined(__linux__)
#include <ctime>
#else
#include <chrono>
#endif

namespace nyla
{

#if defined(__linux__) // TODO: move this into platform layer maybe?

auto GetMonotonicTimeMillis() -> uint64_t
{
    timespec ts{};
    NYLA_ASSERT(clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == 0);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

auto GetMonotonicTimeMicros() -> uint64_t
{
    timespec ts{};
    NYLA_ASSERT(clock_gettime(CLOCK_MONOTONIC_RAW,&ts)== 0);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

auto GetMonotonicTimeNanos() -> uint64_t
{
    timespec ts{};
    NYLA_ASSERT(clock_gettime(CLOCK_MONOTONIC_RAW,& ts)== 0);
    return ts.tv_sec * 1e9 + ts.tv_nsec;
}

#else

namespace
{
inline auto SteadyNow() noexcept
{
    return std::chrono::steady_clock::now().time_since_epoch();
}
} // namespace

auto GetMonotonicTimeMillis() -> std::uint64_t
{
    using namespace std::chrono;
    return (std::uint64_t)duration_cast<milliseconds>(SteadyNow()).count();
}

auto GetMonotonicTimeMicros() -> std::uint64_t
{
    using namespace std::chrono;
    return (std::uint64_t)duration_cast<microseconds>(SteadyNow()).count();
}

auto GetMonotonicTimeNanos() -> std::uint64_t
{
    using namespace std::chrono;
    return (std::uint64_t)duration_cast<nanoseconds>(SteadyNow()).count();
}

#endif

} // namespace nyla