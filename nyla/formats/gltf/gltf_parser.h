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

struct GltfMeshPrimitiveAttribute
{
    std::string_view name;
    uint32_t accessor;
};

struct GltfMeshPrimitive
{
    std::span<GltfMeshPrimitiveAttribute> attributes;
    uint32_t mode;
    uint32_t indices;
    uint32_t material;
};

struct GltfMesh
{
    std::string_view name;
    std::span<GltfMeshPrimitive> primitives;
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

    auto GetBufferViews() -> std::span<GltfBufferView>
    {
        return m_BufferViews;
    }

    auto GetBufferView(uint32_t i) -> GltfBufferView &
    {
        return m_BufferViews[i];
    }

    auto GetBufferViewData(const GltfBufferView &bufferView) -> std::span<char>
    {
        NYLA_ASSERT(bufferView.buffer == 0);
        return m_BinChunk.subspan(bufferView.byteOffset, bufferView.byteLength);
    }

    auto GetAccessors() -> std::span<GltfAccessor>
    {
        return m_Accessors;
    }

    auto GetAccessor(uint32_t i) -> GltfAccessor &
    {
        return m_Accessors[i];
    }

    auto GetMeshes() -> std::span<GltfMesh>
    {
        return m_Meshes;
    }

    auto GetMesh(uint32_t i) -> GltfMesh &
    {
        return m_Meshes[i];
    }

  private:
    RegionAlloc &m_Alloc;
    void *m_Base;
    void *m_At;
    uint32_t m_BytesLeft;

    std::span<char> m_BinChunk;

    InlineVec<GltfBuffer, 1> m_Buffers;
    InlineVec<GltfBufferView, 6> m_BufferViews;
    InlineVec<GltfAccessor, 6> m_Accessors;
    InlineVec<GltfMesh, 1> m_Meshes;
};

} // namespace nyla