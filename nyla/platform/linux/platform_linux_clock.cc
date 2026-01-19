#include "nyla/commons/assert.h"
#include "nyla/platform/linux/platform_linux.h"
#include <ctime>

namespace nyla
{

auto Platform::Impl::GetMonotonicTimeMillis() -> uint64_t
{
    timespec ts{};
    NYLA_ASSERT(clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == 0);
    return ts.tv_sec * 1'000 + ts.tv_nsec / 1'000'000;
}

auto Platform::Impl::GetMonotonicTimeMicros() -> uint64_t
{
    timespec ts{};
    NYLA_ASSERT(clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == 0);
    return ts.tv_sec * 1'000'000 + ts.tv_nsec / 1'000;
}

auto Platform::Impl::GetMonotonicTimeNanos() -> uint64_t
{
    timespec ts{};
    NYLA_ASSERT(clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == 0);
    return ts.tv_sec * 1'000'000'000 + ts.tv_nsec;
}

auto Platform::GetMonotonicTimeMillis() -> uint64_t
{
    return m_Impl->GetMonotonicTimeMillis();
}

auto Platform::GetMonotonicTimeMicros() -> uint64_t
{
    return m_Impl->GetMonotonicTimeMicros();
}

auto Platform::GetMonotonicTimeNanos() -> uint64_t
{
    return m_Impl->GetMonotonicTimeNanos();
}

} // namespace nyla