#pragma once

#include <cstdint>

#include "nyla/commons/byteparser.h"
#include "nyla/commons/region_alloc.h"

namespace nyla
{

struct bdf_glyph
{
    uint32_t encoding;
    // const char *name;
    int32_t dwidth[2];
    int32_t bbx[4];
    span<uint8_t> bitmap;
};

struct bdf_parser : byte_parser
{
};

namespace BdfParser
{

auto NextGlyph(bdf_parser &self, region_alloc &alloc, bdf_glyph &out) -> bool;

}

} // namespace nyla