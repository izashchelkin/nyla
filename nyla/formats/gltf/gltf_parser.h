#pragma once

#include "nyla/commons/containers/inline_vec.h"
#include "nyla/commons/memory/region_alloc.h"
#include <cstdint>

namespace nyla
{

struct GltfBuffer
{
    uint32_t byteLength;
};

struct GltfBufferView
{
    uint32_t buffer;
    uint32_t byteOffset;
    uint32_t byteLength;
};

struct GltfAccessor
{
    uint32_t bufferView;
    uint32_t byteOffset;
    uint32_t componentType;
    uint32_t count;
    std::string_view type;
};

class GltfParser
{
  public:
    GltfParser(RegionAlloc &alloc, void *data, uint32_t byteLength)
        : m_Alloc{alloc}, m_Base{data}, m_At{data}, m_BytesLeft{byteLength}
    {
    }

    auto Parse() -> bool;

    auto PopDWord() -> uint32_t;

  private:
    RegionAlloc &m_Alloc;
    void *m_Base;
    void *m_At;
    uint32_t m_BytesLeft;

    InlineVec<GltfBuffer, 1> m_Buffers;
    InlineVec<GltfBufferView, 6> m_BufferViews;
    InlineVec<GltfAccessor, 6> m_Accessors;
};

} // namespace nyla