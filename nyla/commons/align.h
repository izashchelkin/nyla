#pragma once

#include <cstdint>

#include "nyla/commons/macros.h"
#include "nyla/commons/minmax.h"

namespace nyla
{

constexpr inline uint64_t kMinAlign = 16;
template <typename T> constexpr inline uint64_t required_align_v = Max(alignof(T), kMinAlign);

INLINE constexpr auto AlignedUp(uint64_t n, uint64_t align) -> uint64_t
{
    return (n + align - 1) & ~(align - 1);
}

template <typename T> INLINE constexpr auto AlignedUp(T *ptr, uint64_t align) -> T *
{
    return (T *)AlignedUp((uint64_t)ptr, align);
}

} // namespace nyla