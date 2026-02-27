#include "nyla/commons/assert.h"
#include <cstdint>

namespace nyla
{

constexpr auto ParseHexChar(char ch) -> uint8_t;

constexpr auto ParseHexByte(const char *ch) -> uint8_t
{
    uint8_t ret = 0;
    ret |= ParseHexChar(*ch) << 4;
    ret |= ParseHexChar(*(ch + 1));
    return ret;
}

constexpr auto ParseHexChar(char ch) -> uint8_t
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
        NYLA_ASSERT(false);
        return 0xFF;
    }
    }
}

} // namespace nyla