#pragma once

#include "nyla/commons/byteparser.h"

namespace nyla
{

namespace ByteParser
{

auto ParseDecimal(byte_parser &self, double &outDouble, int64_t &outLong) -> ParseNumberResult
{
    int32_t sign = 1;
    if (Peek(self) == '-')
    {
        sign = -1;
        Advance(self);
    }

    uint64_t integer = 0;
    uint64_t fraction = 0;
    uint64_t fractionCount = 0;

    while (BytesLeft(self) > 0 && IsNumber(Peek(self)))
    {
        integer *= 10;
        integer += Read(self) - '0';
    }

    if (Peek(self) == '.')
    {
        Advance(self);
        while (BytesLeft(self) > 0 && IsNumber(Peek(self)))
        {
            ++fractionCount;
            fraction *= 10;
            fraction += Read(self) - '0';
        }

        auto f = static_cast<double>(fraction);
        for (uint32_t i = 0; i < fractionCount; ++i)
            f /= 10.0;

        f += static_cast<double>(integer);

        outDouble = static_cast<double>(sign * f);
        return ParseNumberResult::Double;
    }
    else
    {
        outLong = static_cast<int64_t>(sign * integer);
        return ParseNumberResult::Long;
    }
}

} // namespace ByteParser

} // namespace nyla