#pragma once

#include <cstdint>
#include <span>

#include "nyla/alloc/region_alloc.h"
#include "nyla/commons/byteparser.h"
#include "nyla/commons/vec.h"

namespace nyla
{

struct BdfGlyph
{
    uint32_t encoding;
    // const char *name;
    int32_t dwidth[2];
    int32_t bbx[4];
    std::span<uint8_t> bitmap;
};

class BdfParser : ByteParser
{
  public:
    void Init(RegionAlloc *alloc, const char *data, uint64_t size)
    {
        m_Alloc = alloc;
        ByteParser::Init(data, size);
    }

    auto NextGlyph(BdfGlyph &out) -> bool;

  private:
    RegionAlloc *m_Alloc;
};

} // namespace nyla