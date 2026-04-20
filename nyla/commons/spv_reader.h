#pragma once

#include <cstdint>

#include "nyla/commons/align.h"
#include "nyla/commons/byteparser.h"
#include "nyla/commons/fmt.h"

namespace nyla
{

struct spv_shader_header
{
    uint32_t version;
    uint32_t generator;
    uint32_t bound;
};

struct spv_reader : byte_parser
{
};

namespace SpvReader
{

[[nodiscard]]
INLINE auto VersionMajor(uint32_t version) -> uint8_t
{
    return (version >> 16) & 0xFF;
}

[[nodiscard]]
INLINE auto VersionMinor(uint32_t version) -> uint8_t
{
    return (version >> 8) & 0xFF;
}

INLINE auto ReadHeader(spv_reader &self) -> spv_shader_header
{
    ASSERT(ByteParser::Read32(self) == 0x07230203);

    uint32_t version = ByteParser::Read32(self);
    DASSERT(version == 67072);

    uint32_t generator = ByteParser::Read32(self);
    DASSERT(generator == 917504);

    uint32_t bound = ByteParser::Read32(self);

    uint32_t reserved = ByteParser::Read32(self);
    DASSERT(reserved == 0);

    return spv_shader_header{
        .version = version,
        .generator = generator,
        .bound = bound,
    };
}

[[nodiscard]]
INLINE auto ReadString(spv_reader &self) -> byteview
{
    byteview str = ByteParser::PeekCStr(self);
    ByteParser::Advance(self, AlignedUp(self.at, 4) - self.at);
    return str;
}

} // namespace SpvReader

} // namespace nyla