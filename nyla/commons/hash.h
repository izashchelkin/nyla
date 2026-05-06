#pragma once

#include <cstdint>

#include "nyla/commons/macros.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

// FNV-1a offset basis. Use as the initial seed when accumulating hashes.
constexpr inline uint32_t kHashInit32 = 0x811C9DC5u;
constexpr inline uint64_t kHashInit64 = 0xCBF29CE484222325ull;

// Boost-style mix. Order-sensitive: HashCombineN(a, b) != HashCombineN(b, a).
INLINE auto HashCombine32(uint32_t seed, uint32_t v) -> uint32_t
{
    seed ^= v + 0x9E3779B9u + (seed << 6) + (seed >> 2);
    return seed;
}

INLINE auto HashCombine64(uint64_t seed, uint64_t v) -> uint64_t
{
    seed ^= v + 0x9E3779B97F4A7C15ull + (seed << 12) + (seed >> 4);
    return seed;
}

// FNV-1a over raw bytes. Stable across platforms; not cryptographic.
[[nodiscard]]
INLINE auto HashBytes32(byteview bytes) -> uint32_t
{
    uint32_t h = kHashInit32;
    for (uint64_t i = 0; i < bytes.size; ++i)
        h = (h ^ bytes.data[i]) * 0x01000193u;
    return h;
}

[[nodiscard]]
INLINE auto HashBytes64(byteview bytes) -> uint64_t
{
    uint64_t h = kHashInit64;
    for (uint64_t i = 0; i < bytes.size; ++i)
        h = (h ^ bytes.data[i]) * 0x100000001B3ull;
    return h;
}

} // namespace nyla
