#pragma once

#include <cstdint>

namespace nyla
{

inline constexpr auto AlignedUp(uint32_t n, uint32_t align) -> uint32_t
{
#if 0
    return n + (align - (n % align)) % align;
#else
    return (n + align - 1) & ~(align - 1);
#endif
}

static_assert(AlignedUp(320, 256) == 512);

inline constexpr void AlignUp(uint32_t &n, uint32_t align)
{
    n = AlignedUp(n, align);
}

} // namespace nyla