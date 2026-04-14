#pragma once

#include <cstdint>

#include "nyla/commons/fmt.h"

namespace nyla
{

constexpr inline auto ParseHexChar(char ch) -> uint8_t
{
    switch (ch)
    {
    case '0':
        return 0;

    case '1':
        return 1;

    case '2':
        return 2;

    case '3':
        return 3;

    case '4':
        return 4;

    case '5':
        return 5;

    case '6':
        return 6;

    case '7':
        return 7;

    case '8':
        return 8;

    case '9':
        return 9;

    case 'a':
    case 'A':
        return 10;

    case 'b':
    case 'B':
        return 11;

    case 'c':
    case 'C':
        return 12;

    case 'd':
    case 'D':
        return 13;

    case 'e':
    case 'E':
        return 14;

    case 'f':
    case 'F':
        return 15;

    default: {
        ASSERT(false);
        return 0xFF;
    }
    }
}

constexpr inline auto ParseHexByte(char ch1, char ch2) -> uint8_t
{
    uint8_t ret = 0;
    ret |= ParseHexChar(ch1) << 4;
    ret |= ParseHexChar(ch2);
    return ret;
}

constexpr inline auto ParseHexByte(const char *ch) -> uint8_t
{
    return ParseHexByte(*ch, *(ch + 1));
}

} // namespace nyla