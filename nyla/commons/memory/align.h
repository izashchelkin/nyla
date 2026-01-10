#pragma once

#include <cstdint>

namespace nyla
{

inline constexpr auto AlignedUp(uint32_t n, uint32_t align) -> uint32_t
{
    // return n + (align - (n % align)) % align;
    return (n + align - 1) & ~(align - 1);
}

static_assert(AlignedUp(320, 256) == 512);

inline constexpr void AlignUp(uint32_t &n, uint32_t align)
{
    n = AlignedUp(n, align);
}

} // namespace nyla