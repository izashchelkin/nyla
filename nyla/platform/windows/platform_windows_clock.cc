#include "nyla/platform/windows/platform_windows.h"
#include <cstdint>
#include <intrin.h>

namespace nyla
{

namespace
{

auto GetPerformanceFreq() -> uint64_t
{
    static const auto freq = [] -> uint64_t {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        return static_cast<uint64_t>(freq.QuadPart);
    }();
    return freq;
}

auto GetPerformanceTicks() -> uint64_t
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<uint64_t>(counter.QuadPart);
}

auto TicksTo(uint64_t ticks, uint64_t scale) -> uint64_t
{
    unsigned __int64 hi = 0;
    unsigned __int64 lo = _umul128(ticks, scale, &hi);

    unsigned __int64 rem = 0;
    unsigned __int64 q = _udiv128(hi, lo, GetPerformanceFreq(), &rem);
    return static_cast<uint64_t>(q);
}

} // namespace

auto Platform::Impl::GetMonotonicTimeMillis() -> uint64_t
{
    return TicksTo(GetPerformanceTicks(), 1'000ULL);
}

auto Platform::GetMonotonicTimeMillis() -> uint64_t
{
    return m_Impl->GetMonotonicTimeMillis();
}

auto Platform::Impl::GetMonotonicTimeMicros() -> uint64_t
{
    return TicksTo(GetPerformanceTicks(), 1'000'000ULL);
}

auto Platform::GetMonotonicTimeMicros() -> uint64_t
{
    return m_Impl->GetMonotonicTimeMicros();
}

auto Platform::Impl::GetMonotonicTimeNanos() -> uint64_t
{
    return TicksTo(GetPerformanceTicks(), 1'000'000'000ULL);
}

auto Platform::GetMonotonicTimeNanos() -> uint64_t
{
    return m_Impl->GetMonotonicTimeNanos();
}

} // namespace nyla