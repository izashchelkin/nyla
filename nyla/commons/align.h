#pragma once

#include <cstdint>

#include "nyla/commons/macros.h"

namespace nyla
{

constexpr inline uint64_t kMinAlign = 16;

template <typename T> constexpr inline uint64_t required_align_v = alignof(T);

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
