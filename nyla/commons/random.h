#pragma once

#include <cstdint>

#include "nyla/commons/fmt.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/platform.h"

namespace nyla
{

INLINE auto Rotl(uint64_t x, uint64_t k) -> uint64_t
{
    return (x << k) | (x >> (64 - k));
}

INLINE auto SeedXoshiro256ss(uint64_t (&s)[4])
{
    for (uint64_t i = 0; i < 4; ++i)
        s[i] = GenRandom64();

    ASSERT(s[0] != 0 || s[1] != 0 || s[2] != 0 || s[3] != 0);
}

INLINE auto Xoshiro256ss(uint64_t (&s)[4]) -> uint64_t
{
    uint64_t ret = Rotl(s[1] * 5, 7) * 9;
    uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = Rotl(s[3], 45);
    return ret;
}

} // namespace nyla