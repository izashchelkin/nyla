#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>
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

INLINE auto BitScanForward32(uint32_t n) -> uint32_t
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

INLINE auto BitScanForward64(uint64_t n) -> uint64_t
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

INLINE auto ByteSwap16(uint16_t val) -> uint16_t
{
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_bswap16(val);
#else
    return _byteswap_ushort(val);
#endif
}

INLINE auto ByteSwap32(uint32_t val) -> uint32_t
{
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_bswap32(val);
#else
    return _byteswap_ulong(val);
#endif
}

INLINE auto ByteSwap64(uint64_t val) -> uint64_t
{
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_bswap64(val);
#else
    return _byteswap_uint64(val);
#endif
}

INLINE auto LRound(double x) -> int64_t
{
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_lround(x);
#else
    double bias;
    if (x >= 0)
        bias = 0.5;
    else
        bias = -0.5;
    return _mm_cvtsd_si64(_mm_set_sd(x + bias));
#endif
}

INLINE auto UMul128(uint64_t a, uint64_t b, uint64_t &hi) -> uint64_t
{
#if defined(__clang__) || defined(__GNUC__)
    unsigned __int128 res = (unsigned __int128)a * b;
    hi = (uint64_t)(res >> 64);
    return (uint64_t)res;
#else
    return _umul128(a, b, &hi);
#endif
}

INLINE auto UDiv128(uint64_t hi, uint64_t lo, uint64_t divisor, uint64_t &reminder) -> uint64_t
{
#if defined(__clang__) || defined(__GNUC__)
    unsigned __int128 dividend = ((unsigned __int128)hi << 64) | lo;
    reminder = (uint64_t)(dividend % divisor);
    return (uint64_t)(dividend / divisor);
#else
    return _udiv128(hi, lo, divisor, &reminder);
#endif
}

INLINE auto Sin(float f) -> float
{
    return sinf(f);
}

INLINE auto Cos(float f) -> float
{
    return cosf(f);
}

INLINE auto SinCos(float f, float &outSin, float &outCos)
{
    outSin = ::sinf(f);
    outCos = ::cosf(f);
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

INLINE auto Pow(float x, float y) -> float
{
    return ::powf(x, y);
}

INLINE auto Exit(int code)
{
    ::quick_exit(code);
}

INLINE auto AtomicLoad32(const uint32_t *p) -> uint32_t
{
#if defined(__clang__) || defined(__GNUC__)
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
#else
    return (uint32_t)_InterlockedOr((volatile long *)p, 0);
#endif
}

INLINE void AtomicStore32(uint32_t *p, uint32_t v)
{
#if defined(__clang__) || defined(__GNUC__)
    __atomic_store_n(p, v, __ATOMIC_RELEASE);
#else
    _InterlockedExchange((volatile long *)p, (long)v);
#endif
}

INLINE auto AtomicLoad64(const uint64_t *p) -> uint64_t
{
#if defined(__clang__) || defined(__GNUC__)
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
#else
    return (uint64_t)_InterlockedOr64((volatile long long *)p, 0);
#endif
}

INLINE void AtomicStore64(uint64_t *p, uint64_t v)
{
#if defined(__clang__) || defined(__GNUC__)
    __atomic_store_n(p, v, __ATOMIC_RELEASE);
#else
    _InterlockedExchange64((volatile long long *)p, (long long)v);
#endif
}

} // namespace nyla