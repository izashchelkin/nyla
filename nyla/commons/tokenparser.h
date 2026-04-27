#pragma once

#include <cstdint>

#include "nyla/commons/byteparser.h"
#include "nyla/commons/hex.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/span_def.h"
#include "nyla/commons/stringparser.h"

namespace nyla
{

INLINE auto IsHexDigit(uint8_t ch) -> bool
{
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

INLINE auto IsIdentifierStart(uint8_t ch) -> bool
{
    return IsAlpha(ch) || ch == '_';
}

INLINE auto IsIdentifierCont(uint8_t ch) -> bool
{
    return IsAlpha(ch) || IsNumber(ch) || ch == '_';
}

namespace TokenParser
{

INLINE void SkipLineWhitespace(byte_parser &self)
{
    while (ByteParser::HasNext(self))
    {
        const uint8_t ch = ByteParser::Peek(self);
        if (ch != ' ' && ch != '\t')
            break;
        ByteParser::Advance(self);
    }
}

INLINE auto SkipLineComment(byte_parser &self, uint8_t commentChar = '#') -> bool
{
    if (!ByteParser::HasNext(self) || ByteParser::Peek(self) != commentChar)
        return false;
    ByteParser::NextLine(self);
    return true;
}

[[nodiscard]]
INLINE auto ParseIdentifier(byte_parser &self) -> byteview
{
    ASSERT(ByteParser::HasNext(self) && IsIdentifierStart(ByteParser::Peek(self)));

    const uint8_t *start = self.at;
    ByteParser::Advance(self);
    while (ByteParser::HasNext(self) && IsIdentifierCont(ByteParser::Peek(self)))
        ByteParser::Advance(self);

    return byteview{start, (uint64_t)(self.at - start)};
}

[[nodiscard]]
INLINE auto ParseHexU64(byte_parser &self) -> uint64_t
{
    (void)(ByteParser::StartsWithAdvance(self, "0x"_s) || ByteParser::StartsWithAdvance(self, "0X"_s));

    ASSERT(ByteParser::HasNext(self) && IsHexDigit(ByteParser::Peek(self)));

    uint64_t value = 0;
    uint32_t digits = 0;
    while (ByteParser::HasNext(self) && IsHexDigit(ByteParser::Peek(self)))
    {
        ASSERT(digits < 16);
        value = (value << 4) | ParseHexChar(ByteParser::Read(self));
        ++digits;
    }
    return value;
}

} // namespace TokenParser

} // namespace nyla
