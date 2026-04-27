#pragma once

#include <cstdint>

#include "nyla/commons/macros.h"
#include "nyla/commons/minmax.h"

namespace nyla
{

constexpr inline uint64_t kMinAlign = 16;

#if 0
template <typename T> constexpr inline uint64_t required_align_v = Max(alignof(T), kMinAlign);
#else
template <typename T> constexpr inline uint64_t required_align_v = alignof(T);
#endif

INLINE constexpr auto AlignedUp(uint64_t n, uint64_t align) -> uint64_t
{
    return (n + align - 1) & ~(align - 1);
}

template <typename T> INLINE constexpr auto AlignedUp(T *ptr, uint64_t align) -> T *
{
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    return (T *)AlignedUp((uint64_t)ptr, align);
}

} // namespace nyla