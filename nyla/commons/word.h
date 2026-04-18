#pragma once

#include <cstdint>

#include "nyla/commons/macros.h"

namespace nyla
{

INLINE constexpr auto DWord(const char str[4]) -> uint32_t
{
    uint64_t out = 0;
    for (uint64_t i = 0; i < 4; ++i)
        out |= str[i] << (i * 8);
    return out;
}
static_assert(DWord("JSON") == 0x4E4F534A);
static_assert(DWord("BIN\0") == 0x004E4942);

INLINE constexpr auto DWordBE(const char str[4]) -> uint32_t
{
    uint64_t out = 0;
    for (uint64_t i = 0; i < 4; ++i)
        out |= str[i] << ((3 - i) * 8);
    return out;
}
static_assert(DWordBE("JSON") == 0x4A534F4E);
static_assert(DWordBE("BIN\0") == 0x42494E00);

INLINE constexpr auto QWord(const char str[8]) -> uint64_t
{
    uint64_t out = 0;
    for (uint64_t i = 0; i < 8; ++i)
        out |= str[i] << (i * 8);
    return out;
}

INLINE constexpr auto QWordBE(const char str[8]) -> uint64_t
{
    uint64_t out = 0;
    for (uint64_t i = 0; i < 8; ++i)
        out |= str[i] << ((7 - i) * 8);
    return out;
}

} // namespace nyla