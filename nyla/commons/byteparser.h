#pragma once

#include <cstdint>

#include "nyla/commons/fmt.h"
#include "nyla/commons/span.h"

namespace nyla
{

INLINE uint16_t ByteSwap16(uint16_t val)
{
#if defined(_MSC_VER)
    return _byteswap_ushort(val);
#else
    return __builtin_bswap16(val);
#endif
}

INLINE uint32_t ByteSwap32(uint32_t val)
{
#if defined(_MSC_VER)
    return _byteswap_ulong(val);
#else
    return __builtin_bswap32(val);
#endif
}

INLINE uint64_t ByteSwap64(uint64_t val)
{
#if defined(_MSC_VER)
    return _byteswap_uint64(val);
#else
    return __builtin_bswap64(val);
#endif
}

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
    return ch == ' ' || ch == '\n' || ch == '\t';
}

namespace ByteParser
{
struct Instance
{
    const uint8_t *m_At;
    uint64_t m_Left;
};

INLINE void Init(Instance &self, const uint8_t *base, uint64_t size)
{
    self.m_At = base;
    self.m_Left = size;
}

INLINE auto Left(const Instance &self) -> uint64_t
{
    return self.m_Left;
}

[[nodiscard]]
INLINE auto Peek(const Instance &self) -> const uint8_t &
{
    return *self.m_At;
}

INLINE void Advance(Instance &self, uint64_t i)
{
    self.m_At += i;
    self.m_Left -= i;
    NYLA_DASSERT(self.m_Left >= 0);
}

INLINE void Advance(Instance &self)
{
    Advance(self, 1);
}

INLINE auto Read(Instance &self) -> uint8_t
{
    const uint8_t ret = Peek(self);
    Advance(self);
    return ret;
}

template <typename T> auto Read(Instance &self) -> T
{
    const T ret = LoadU<T>(self.m_At);
    Advance(self, sizeof(ret));
    return ret;
}

INLINE auto Read16(Instance &self) -> uint16_t
{
    return Read<uint16_t>(self);
}

INLINE auto Read16BE(Instance &self) -> uint16_t
{
    return ByteSwap16(Read16(self));
}

INLINE auto Read32(Instance &self) -> uint32_t
{
    return Read<uint32_t>(self);
}

INLINE auto Read32BE(Instance &self) -> uint32_t
{
    return ByteSwap32(Read32(self));
}

INLINE auto Read64(Instance &self) -> uint64_t
{
    return Read<uint64_t>(self);
}

INLINE auto Read64BE(Instance &self) -> uint64_t
{
    return ByteSwap64(Read64(self));
}

//

INLINE void SkipUntil(Instance &self, uint8_t ch)
{
    while (Peek(self) != ch)
        Advance(self);
}

INLINE void NextLine(Instance &self)
{
    while (Read(self) != '\n')
        ;
}

INLINE void SkipWhitespace(Instance &self)
{
    while (IsWhitespace(Peek(self)))
        Advance(self);
}

INLINE auto StartsWith(Instance &self, Str str) -> bool
{
    return AsStr((const char *)self.m_At, self.m_Left).StartsWith(str);
}

INLINE auto StartsWithAdvance(Instance &self, Str str) -> bool
{
    if (StartsWith(self, str))
    {
        Advance(self, str.Size());
        return true;
    }
    else
    {
        return false;
    }
}

enum ParseNumberResult
{
    Double,
    Long,
};

auto ParseDecimal(Instance &self, double &outDouble, int64_t &outLong) -> ParseNumberResult;

INLINE auto ParseLong(Instance &self) -> int64_t
{
    double d;
    int64_t l;
    const ParseNumberResult res = ParseDecimal(self, d, l);
    NYLA_ASSERT(res == ParseNumberResult::Long);
    return l;
}

}; // namespace ByteParser

} // namespace nyla