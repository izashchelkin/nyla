#pragma once

#include <cstdint>

namespace nyla
{

inline auto AlignedUp(uint32_t n, uint32_t align) -> uint32_t
{
    // return n + (align - (n % align)) % align;
    return (n + align - 1) & ~(align - 1);
}

inline void AlignUp(uint32_t &n, uint32_t align)
{
    n = AlignedUp(n, align);
}

} // namespace nyla