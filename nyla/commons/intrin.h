#ifndef NYLA_FMT_H
#define NYLA_FMT_H

#include <cmath>
#include <cstdint>
#include <type_traits>

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
    ::_BitScanForward64(&index, n);
    return (uint32_t)index;
#endif
}

INLINE uint16_t ByteSwap16(uint16_t val)
{
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_bswap16(val);
#else
    return _byteswap_ushort(val);
#endif
}

INLINE uint32_t ByteSwap32(uint32_t val)
{
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_bswap32(val);
#else
    return _byteswap_ulong(val);
#endif
}

INLINE uint64_t ByteSwap64(uint64_t val)
{
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_bswap64(val);
#else
    return _byteswap_uint64(val);
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
    return cosf(f);
}

INLINE auto Sin(float f) -> float
{
    return sinf(f);
}

INLINE auto Tan(float f) -> float
{
    return tanf(f);
}

INLINE auto ATan(float f) -> float
{
    return atanf(f);
}

INLINE auto Sqrt(float val) -> float
{
    __m128 v = _mm_set_ss(val);
    v = _mm_sqrt_ss(v);
    return _mm_cvtss_f32(v);
}

} // namespace nyla

#endif