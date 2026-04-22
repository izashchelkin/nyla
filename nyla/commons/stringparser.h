#pragma once

#include "nyla/commons/byteparser.h"
#include <cstdint>

namespace nyla
{

INLINE auto IsNumber(uint8_t ch) -> bool
{
    return ch >= '0' && ch <= '9';
}

INLINE auto IsAlpha(uint8_t ch) -> bool
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch < 'Z');
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

INLINE auto ParseLong(byte_parser &self) -> int64_t;

inline auto ParseDecimal(byte_parser &self, double &outDouble, int64_t &outLong) -> ParseNumberResult
{
    int32_t sign = 1;
    if (ByteParser::Peek(self) == '-')
    {
        sign = -1;
        ByteParser::Advance(self);
    }

    uint64_t integer = 0;
    uint64_t fraction = 0;
    uint64_t fractionCount = 0;

    while (ByteParser::BytesLeft(self) > 0 && IsNumber(ByteParser::Peek(self)))
    {
        integer *= 10;
        integer += ByteParser::Read(self) - '0';
    }

    if (ByteParser::Peek(self) == '.')
    {
        ByteParser::Advance(self);
        while (ByteParser::BytesLeft(self) > 0 && IsNumber(ByteParser::Peek(self)))
        {
            ++fractionCount;
            fraction *= 10;
            fraction += ByteParser::Read(self) - '0';
        }

        int64_t powerOfTen = 0;

        if (ByteParser::BytesLeft(self) > 0 && ByteParser::Peek(self) == 'e')
        {
            ByteParser::Advance(self);
            powerOfTen = ParseLong(self);
        }

        auto f = static_cast<double>(fraction);
        for (uint32_t i = 0; i < fractionCount; ++i)
            f /= 10.0;

        f += static_cast<double>(integer);

        f *= Pow(10, (float)powerOfTen);

        outDouble = static_cast<double>(sign * f);
        return ParseNumberResult::Double;
    }
    else
    {
        outLong = static_cast<int64_t>(sign * integer);
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