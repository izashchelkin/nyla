#pragma once

#include <cstdint>

#include "nyla/commons/fmt.h"
#include "nyla/commons/macros.h"

namespace nyla
{

constexpr INLINE auto GetHexChar(uint8_t b) -> uint8_t
{
    return "0123456789ABCDEF"[b & 0xF];
}

constexpr INLINE auto ParseHexChar(uint8_t ch) -> uint8_t
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;

    ASSERT(false);
}

constexpr INLINE auto ParseHexByte(uint8_t ch1, uint8_t ch2) -> uint8_t
{
    uint8_t ret = 0;
    ret |= ParseHexChar(ch1) << 4;
    ret |= ParseHexChar(ch2);
    return ret;
}

constexpr INLINE auto ParseHexByte(const uint8_t *ch) -> uint8_t
{
    return ParseHexByte(*ch, *(ch + 1));
}

} // namespace nyla