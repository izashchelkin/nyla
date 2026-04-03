#pragma once

#include <cstdint>
#include <span>

#include "nyla/commons/region_alloc.h"
#include "nyla/commons/byteparser.h"

namespace nyla
{

struct BdfGlyph
{
    uint32_t encoding;
    // const char *name;
    int32_t dwidth[2];
    int32_t bbx[4];
    Span<uint8_t> bitmap;
};

class BdfParser : ByteParser
{
  public:
    void Init(const char *data, uint64_t size)
    {
        ByteParser::Init(data, size);
    }

    auto NextGlyph(RegionAlloc *alloc, BdfGlyph &out) -> bool;
};

} // namespace nyla