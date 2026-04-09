#pragma once

#include <cstdint>
#include <type_traits>

#include "nyla/commons/fmt.h"
#include "nyla/commons/macros.h"

#if defined(_MSC_VER)
#include <intrin.h> // IWYU pragma: export
#else
#include <x86intrin.h> // IWYU pragma: export
#endif

namespace nyla
{

template <typename T>
INLINE void LoadU(const void *ptr, T &out)
    requires(std::is_trivially_constructible_v<T> and std::is_trivially_copyable_v<T>)
{

#if defined(_MSC_VER)
    out = *(const __unaligned T *)(ptr);
#else
    __builtin_memcpy(&out, ptr, sizeof(T));
#endif
};

template <typename T> INLINE auto LoadU(const void *ptr) -> T
{
    T ret;
    LoadU<T>(ptr, ret);
    return ret;
};

INLINE auto Load64U(const void *ptr) -> uint64_t
{
    return LoadU<uint64_t>(ptr);
}

template <typename T>
INLINE void WriteU(void *ptr, const T &val)
    requires(std::is_trivially_constructible_v<T>() and std::is_trivially_copyable_v<T>)
{

#if defined(_MSC_VER)
    *(__unaligned T *)(ptr) = val;
#else
    __builtin_memcpy(ptr, &val, sizeof(T));
#endif
}

INLINE uint32_t BitScanForward32(uint32_t n)
{
    NYLA_DASSERT(n != 0);

#if defined(_MSC_VER)
    unsigned long index;
    _BitScanForward(&index, (unsigned long)n);
    return (uint32_t)index;
#else
    return __builtin_ctz((unsigned int)n);
#endif
}

INLINE uint32_t BitScanForward64(uint64_t n)
{
    NYLA_DASSERT(n != 0);

#if defined(_MSC_VER)
    unsigned long index;
    _BitScanForward64(&index, (unsigned __int64)n);
    return (uint32_t)index;
#else
    return __builtin_ctzll((unsigned long long)n);
#endif
}

} // namespace nyla