#pragma once

#include <cstdint>

#include "nyla/commons/fmt.h"
#include "nyla/commons/intrin.h"
#include "nyla/commons/mem.h"
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

struct byte_parser
{
    const uint8_t *begin;
    const uint8_t *end;
    const uint8_t *at;
};

namespace ByteParser
{

INLINE void Init(byte_parser &self, const uint8_t *base, uint64_t size)
{
    self.begin = base;
    self.at = base;
    self.end = base + size;
}

INLINE auto BytesLeft(const byte_parser &self) -> uint64_t
{
    return self.end - self.at;
}

INLINE auto HasNext(const byte_parser &self) -> uint64_t
{
    return self.end > self.at;
}

[[nodiscard]]
INLINE auto Peek(const byte_parser &self) -> const uint8_t &
{
    return *self.at;
}

INLINE void Advance(byte_parser &self, uint64_t i)
{
    self.at += i;
    DASSERT(self.at < self.end);
}

INLINE void Advance(byte_parser &self)
{
    Advance(self, 1);
}

INLINE auto Read(byte_parser &self) -> uint8_t
{
    const uint8_t ret = Peek(self);
    Advance(self);
    return ret;
}

INLINE auto ReadOrDefault(byte_parser &self, uint8_t defaultValue) -> uint8_t
{
    if (HasNext(self))
        return Read(self);
    else
        return defaultValue;
}

template <typename T> INLINE auto Read(byte_parser &self) -> T
{
    const T ret = LoadU<T>(self.at);
    Advance(self, sizeof(ret));
    return ret;
}

INLINE auto ReadN(byte_parser &self, void *out, uint64_t count) -> bool
{
    if (BytesLeft(self) >= count)
    {
        MemCpy(out, self.at, count);
        Advance(self, count);
        return true;
    }
    else
    {
        return false;
    }
}

INLINE auto Read16(byte_parser &self) -> uint16_t
{
    return Read<uint16_t>(self);
}

INLINE auto Read16BE(byte_parser &self) -> uint16_t
{
    return ByteSwap16(Read16(self));
}

INLINE auto Read32(byte_parser &self) -> uint32_t
{
    return Read<uint32_t>(self);
}

INLINE auto Read32BE(byte_parser &self) -> uint32_t
{
    return ByteSwap32(Read32(self));
}

INLINE auto Read64(byte_parser &self) -> uint64_t
{
    return Read<uint64_t>(self);
}

INLINE auto Read64BE(byte_parser &self) -> uint64_t
{
    return ByteSwap64(Read64(self));
}

//

INLINE void SkipUntil(byte_parser &self, uint8_t ch)
{
    while (Peek(self) != ch)
        Advance(self);
}

INLINE void NextLine(byte_parser &self)
{
    while (Read(self) != '\n')
        ;
}

INLINE void SkipWhitespace(byte_parser &self)
{
    while (IsWhitespace(Peek(self)))
        Advance(self);
}

INLINE auto StartsWith(byte_parser &self, byteview prefix) -> bool
{
    return MemStartsWith(self.at, BytesLeft(self), prefix.data, prefix.size);
}

INLINE auto StartsWithAdvance(byte_parser &self, byteview prefix) -> bool
{
    if (StartsWith(self, prefix))
    {
        Advance(self, prefix.size);
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

auto ParseDecimal(byte_parser &self, double &outDouble, int64_t &outLong) -> ParseNumberResult;

INLINE auto ParseLong(byte_parser &self) -> int64_t
{
    double d;
    int64_t l;
    const ParseNumberResult res = ParseDecimal(self, d, l);
    ASSERT(res == ParseNumberResult::Long);
    return l;
}

}; // namespace ByteParser

} // namespace nyla