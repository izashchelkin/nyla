#pragma once

#include <cstdint>

#include "nyla/commons/byteparser.h"

namespace nyla::ByteParser::PNGParser
{

enum ScanType : uint8_t
{
    Load,
    Type,
    Header,
};

struct PNGChunk
{
    uint32_t length;
    uint32_t type;
};

struct Instance : ByteParser::Instance
{
    uint32_t img_x;
    uint32_t img_y;
    uint32_t img_n;
    uint32_t img_out_n;
    uint32_t m_Depth;

    uint8_t *img_buffer;
    uint8_t *img_buffer_end;
    uint8_t *img_buffer_original;
    uint8_t *img_buffer_original_end;

    uint8_t *idata;
    uint8_t *expanded;
    uint8_t *out;
};

INLINE void Init(Instance &self, const uint8_t *base, uint64_t size)
{
    ByteParser::Init(self, base, size);
}

static auto Parse(Instance &self, ScanType scan) -> bool;

} // namespace nyla::ByteParser::PNGParser
