#pragma once

#include <cstdint>

#include "nyla/commons/fmt.h"
#include "nyla/commons/intrin.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

struct byte_parser
{
    const uint8_t *begin;
    const uint8_t *end;
    const uint8_t *at;
};

namespace ByteParser
{

INLINE void Init(byte_parser &self, const void *base, uint64_t size)
{
    self.begin = (uint8_t *)base;
    self.at = (uint8_t *)base;
    self.end = (uint8_t *)base + size;
}

INLINE auto BytesLeft(const byte_parser &self) -> uint64_t
{
    return self.end - self.at;
}

INLINE auto HasNext(const byte_parser &self) -> bool
{
    return self.end > self.at;
}

[[nodiscard]]
INLINE auto Peek(const byte_parser &self) -> uint8_t
{
    return *self.at;
}

INLINE void Advance(byte_parser &self, uint64_t i)
{
    self.at += i;
    DASSERT(self.at <= self.end);
}

INLINE void Advance(byte_parser &self)
{
    Advance(self, 1);
}

template <typename T = uint8_t> INLINE auto Read(byte_parser &self) -> T
{
    const T ret = LoadU<T>(self.at);
    Advance(self, sizeof(ret));
    return ret;
}

INLINE auto ReadOrDefault(byte_parser &self, uint8_t defaultValue) -> uint8_t
{
    if (HasNext(self))
        return Read(self);
    else
        return defaultValue;
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

[[nodiscard]]
INLINE auto ReadBytes(byte_parser &self, uint64_t count) -> byteview
{
    DASSERT(BytesLeft(self) >= count);
    byteview ret{self.at, count};
    self.at += count;
    return ret;
}

[[nodiscard]]
INLINE auto ReadCStr(byte_parser &self) -> byteview
{
    uint64_t len = CStrLen(self.at, self.end - self.at);
    byteview ret{self.at, len};
    self.at += len;
    if (self.at < self.end)
        ++self.at;
    return ret;
}

INLINE void SkipUntil(byte_parser &self, uint8_t ch)
{
    while (HasNext(self) && Peek(self) != ch)
        Advance(self);
}

INLINE void SkipPast(byte_parser &self, uint8_t ch)
{
    while (HasNext(self) && Read(self) != ch)
        ;
}

INLINE void NextLine(byte_parser &self)
{
    SkipPast(self, '\n');
}

INLINE auto StartsWith(const byte_parser &self, byteview prefix) -> bool
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

}; // namespace ByteParser

} // namespace nyla
