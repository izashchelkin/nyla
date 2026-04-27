#pragma once

#include "nyla/commons/byteparser.h"
#include "nyla/commons/macros.h"
#include <cstdint>

namespace nyla
{

INLINE auto IsNumber(uint8_t ch) -> bool
{
    return ch >= '0' && ch <= '9';
}

INLINE auto IsAlpha(uint8_t ch) -> bool
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

INLINE auto IsWhitespace(uint8_t ch) -> bool
{
    switch (ch)
    {
    case ' ':
    case '\n':
    case '\r':
    case '\t':
        return true;

    default:
        return false;
    }
}

namespace StringParser
{

INLINE void SkipWhitespace(byte_parser &self)
{
    while (IsWhitespace(ByteParser::Peek(self)))
        ByteParser::Advance(self);
}

enum ParseNumberResult
{
    Double,
    Long,
};

// Defined in stringparser.cc. Linker drops it (and the libc strtod reference)
// when no caller pulls ParseDecimal/ParseLong into the program.
auto ParseDoubleFromBytes(const uint8_t *data, uint64_t size) -> double;

inline auto ParseDecimal(byte_parser &self, double &outDouble, int64_t &outLong) -> ParseNumberResult
{
    const uint8_t *start = self.at;

    bool negative = false;
    if (ByteParser::HasNext(self) && ByteParser::Peek(self) == '-')
    {
        negative = true;
        ByteParser::Advance(self);
    }

    uint64_t mantissa = 0;
    int32_t digitCount = 0;
    bool overflow = false;

    while (ByteParser::HasNext(self) && IsNumber(ByteParser::Peek(self)))
    {
        uint8_t digit = ByteParser::Read(self) - '0';
        if (digitCount < 19)
        {
            mantissa = mantissa * 10 + digit;
            ++digitCount;
        }
        else
        {
            overflow = true;
        }
    }

    bool hasDot = false;
    if (ByteParser::HasNext(self) && ByteParser::Peek(self) == '.')
    {
        hasDot = true;
        ByteParser::Advance(self);
        while (ByteParser::HasNext(self) && IsNumber(ByteParser::Peek(self)))
            ByteParser::Advance(self);
    }

    bool hasExp = false;
    if (ByteParser::HasNext(self) && (ByteParser::Peek(self) == 'e' || ByteParser::Peek(self) == 'E'))
    {
        hasExp = true;
        ByteParser::Advance(self);
        if (ByteParser::HasNext(self) && (ByteParser::Peek(self) == '+' || ByteParser::Peek(self) == '-'))
            ByteParser::Advance(self);
        while (ByteParser::HasNext(self) && IsNumber(ByteParser::Peek(self)))
            ByteParser::Advance(self);
    }

    uint64_t int64AbsLimit;
    if (negative)
        int64AbsLimit = (uint64_t)INT64_MAX + 1;
    else
        int64AbsLimit = (uint64_t)INT64_MAX;
    if (hasDot || hasExp || overflow || mantissa > int64AbsLimit)
    {
        outDouble = ParseDoubleFromBytes(start, (uint64_t)(self.at - start));
        return ParseNumberResult::Double;
    }
    else
    {
        if (negative)
            outLong = (int64_t)(0u - mantissa);
        else
            outLong = (int64_t)mantissa;
        return ParseNumberResult::Long;
    }
}

INLINE auto ParseLong(byte_parser &self) -> int64_t
{
    double d;
    int64_t l;
    const ParseNumberResult res = ParseDecimal(self, d, l);
    ASSERT(res == ParseNumberResult::Long);
    return l;
}

} // namespace StringParser

} // namespace nyla