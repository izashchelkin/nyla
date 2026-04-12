#pragma once

#include <cstdint>

#include "nyla/commons/intrin.h"

namespace nyla
{

constexpr auto CeilDiv(uint32_t a, uint32_t b) -> uint32_t
{
    return (a + b - 1) / b;
}

constexpr auto CeilDiv(uint64_t a, uint64_t b) -> uint64_t
{
    return (a + b - 1) / b;
}

inline auto Sqrt(float val) -> float
{
#if defined(_MSC_VER)
    __m128 v = _mm_set_ss(val);
    v = _mm_sqrt_ss(v);
    return _mm_cvtss_f32(v);
#else
    return __builtin_sqrtf(val);
#endif
}

inline auto Sqrt(double val) -> double
{
#if defined(_MSC_VER)
    __m128d v = _mm_set_sd(val);
    v = _mm_sqrt_sd(v, v);
    return _mm_cvtsd_f64(v);
#else
    return __builtin_sqrt(val);
#endif
}

} // namespace nyla