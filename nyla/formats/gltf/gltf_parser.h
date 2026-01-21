#pragma once

#include "nyla/commons/memory/region_alloc.h"
#include <cstdint>

namespace nyla
{

class GltfParser
{
  public:
    GltfParser(RegionAlloc &alloc, void *data, uint32_t byteLength)
        : m_Alloc{alloc}, m_At{data}, m_BytesLeft{byteLength}
    {
    }

    auto Parse() -> bool;

    auto PopDWord() -> uint32_t;

  private:
    RegionAlloc &m_Alloc;
    void *m_At;
    uint32_t m_BytesLeft;
};

} // namespace nyla