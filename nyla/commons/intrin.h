#ifndef NYLA_FMT_H
#define NYLA_FMT_H

#include <cstdint>
#include <type_traits>

#if defined(__clang__) || defined(__GNUC__)
#else
#include <cmath>
#endif

#include <immintrin.h>

#include "nyla/commons/fmt.h"
#include "nyla/commons/macros.h"

namespace nyla
{

template <typename T>
INLINE void LoadU(const void *ptr, T &out)
    requires(std::is_trivially_copyable_v<T>)
{
#if defined(__clang__) || defined(__GNUC__)
    __builtin_memcpy(&out, ptr, sizeof(T));
#else
    out = *(const __unaligned T *)(ptr);
#endif
}

template <typename T> INLINE auto LoadU(const void *ptr) -> T
{
    T ret;
    LoadU<T>(ptr, ret);
    return ret;
}

template <typename T>
INLINE void WriteU(void *ptr, const T &val)
    requires(std::is_trivially_copyable_v<T>)
{
#if defined(__clang__) || defined(__GNUC__)
    __builtin_memcpy(ptr, &val, sizeof(T));
#else
    *(__unaligned T *)(ptr) = val;
#endif
}

INLINE uint32_t BitScanForward32(uint32_t n)
{
    DASSERT(n != 0);
#if defined(__clang__) || defined(__GNUC__)
    return (uint32_t)__builtin_ctz(n);
#else
    unsigned long index;
    _BitScanForward(&index, (unsigned long)n);
    return (uint32_t)index;
#endif
}

INLINE uint32_t BitScanForward64(uint64_t n)
{
    DASSERT(n != 0);
#if defined(__clang__) || defined(__GNUC__)
    return (uint32_t)__builtin_ctzll(n);
#else
    unsigned long index;
    _BitScanForward64(&index, n);
    return (uint32_t)index;
#endif
}

INLINE int64_t LRound(double x)
{
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_lround(x);
#else
    return _mm_cvtsd_si64(_mm_set_sd(x + (x >= 0 ? 0.5 : -0.5)));
#endif
}

INLINE uint64_t UMul128(uint64_t a, uint64_t b, uint64_t &hi)
{
#if defined(__clang__) || defined(__GNUC__)
    unsigned __int128 res = (unsigned __int128)a * b;
    hi = (uint64_t)(res >> 64);
    return (uint64_t)res;
#else
    return _umul128(a, b, &hi);
#endif
}

INLINE uint64_t UDiv128(uint64_t hi, uint64_t lo, uint64_t divisor, uint64_t &reminder)
{
#if defined(__clang__) || defined(__GNUC__)
    unsigned __int128 dividend = ((unsigned __int128)hi << 64) | lo;
    reminder = (uint64_t)(dividend % divisor);
    return (uint64_t)(dividend / divisor);
#else
    return _udiv128(hi, lo, divisor, &reminder);
#endif
}

INLINE auto Cos(float f) -> float
{
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_cosf(f);
#else
    return cosf(f);
#endif
}

INLINE auto Sin(float f) -> float
{
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_sinf(f);
#else
    return sinf(f);
#endif
}

INLINE auto Sqrt(float val) -> float
{
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_sqrtf(val);
#else
    __m128 v = _mm_set_ss(val);
    v = _mm_sqrt_ss(v);
    return _mm_cvtss_f32(v);
#endif
}

INLINE auto Sqrt(double val) -> double
{
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_sqrt(val);
#else
    __m128d v = _mm_set_sd(val);
    v = _mm_sqrt_sd(v, v);
    return _mm_cvtsd_f64(v);
#endif
}

} // namespace nyla

#endif