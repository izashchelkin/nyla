#pragma once

#include "nyla/commons/region_alloc.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/json/json.h"
#include <cstdint>

namespace nyla
{

struct GltfBuffer
{
    uint32_t byteLength;
};

struct GltfImage
{
    Str uri;
    Str mimeType;
    Str name;
};

struct GltfBufferView
{
    uint32_t buffer;
    uint32_t byteOffset;
    uint32_t byteLength;
};

enum class GltfAccessorComponentType
{
    BYTE = 5120,
    UNSIGNED_BYTE = 5121,
    SHORT = 5122,
    UNSIGNED_SHORT = 5123,
    UNSIGNED_INT = 5125,
    FLOAT = 5126,
};

inline auto GetGltfAccessorComponentSize(GltfAccessorComponentType componentType) -> uint32_t
{
    switch (componentType)
    {
    case GltfAccessorComponentType::BYTE:
    case GltfAccessorComponentType::UNSIGNED_BYTE:
        return 1;

    case GltfAccessorComponentType::SHORT:
    case GltfAccessorComponentType::UNSIGNED_SHORT:
        return 2;

    case GltfAccessorComponentType::UNSIGNED_INT:
    case GltfAccessorComponentType::FLOAT:
        return 4;
    }
}

enum class GltfAccessorType
{
    SCALAR,
    VEC2,
    VEC3,
    VEC4,
    MAT2,
    MAT3,
    MAT4,
};

inline auto GetGltfAccessorComponentCount(GltfAccessorType accessorType) -> uint32_t
{
    switch (accessorType)
    {
    case GltfAccessorType::SCALAR:
        return 1;

    case GltfAccessorType::VEC2:
        return 2;

    case GltfAccessorType::VEC3:
        return 3;

    case GltfAccessorType::VEC4:
    case GltfAccessorType::MAT2:
        return 4;

    case GltfAccessorType::MAT3:
        return 9;

    case GltfAccessorType::MAT4:
        return 16;
    }
}

struct GltfAccessor
{
    uint32_t bufferView;
    uint32_t byteOffset;
    GltfAccessorComponentType componentType;
    uint32_t count;
    GltfAccessorType type;
};

inline auto GetGltfAccessorSize(const GltfAccessor &accessor)
{
    return GetGltfAccessorComponentCount(accessor.type) * GetGltfAccessorComponentSize(accessor.componentType);
}

struct GltfMeshPrimitiveAttribute
{
    Str name;
    uint32_t accessor;
};

struct GltfMeshPrimitive
{
    Span<GltfMeshPrimitiveAttribute> attributes;
    uint32_t mode;
    uint32_t indices;
    uint32_t material;
};

struct GltfMesh
{
    Str name;
    Span<GltfMeshPrimitive> primitives;
};

#if 0
class GlbChunkParser : public ByteParser
{
  public:
    void Init(void *data, uint64_t size)
    {
        m_Base = (uint32_t *)data;
        ByteParser::Init((char *)m_Base, size);
    }

    auto Parse(Span<char> &jsonChunk, Span<char> &binChunk) -> bool;

  private:
    void *m_Base;
    void *m_At;
    uint64_t m_BytesLeft;
};
#endif

class GltfParser
{
  public:
    void Init(RegionAlloc *alloc, Span<char> jsonChunk, Span<char> binChunk)
    {
        m_Alloc = alloc;
        m_JsonChunk = jsonChunk;
        m_BinChunk = binChunk;
    }

    auto Parse() -> bool;

    auto FindAttributeAccessor(Span<GltfMeshPrimitiveAttribute> attributes, Str attributeName,
                               GltfAccessor &out) -> bool;

    auto GetBufferViews() -> Span<GltfBufferView>
    {
        return m_BufferViews.GetSpan();
    }

    auto GetBufferView(uint32_t i) -> GltfBufferView &
    {
        return m_BufferViews[i];
    }

    auto GetBufferViewData(const GltfBufferView &bufferView) -> Span<char>
    {
        NYLA_ASSERT(bufferView.buffer == 0);
        return m_BinChunk.SubSpan(bufferView.byteOffset, bufferView.byteLength);
    }

    auto GetAccessorData(const GltfAccessor &accessor) -> Span<char>
    {
        return GetBufferViewData(GetBufferView(accessor.bufferView)).SubSpan(accessor.byteOffset);
    }

    auto GetAccessors() -> Span<GltfAccessor>
    {
        return m_Accessors.GetSpan();
    }

    auto GetAccessor(uint32_t i) -> GltfAccessor &
    {
        return m_Accessors[i];
    }

    auto GetImages() -> Span<GltfImage>
    {
        return m_Images.GetSpan();
    }

    auto GetMeshes() -> Span<GltfMesh>
    {
        return m_Meshes.GetSpan();
    }

    auto GetMesh(uint32_t i) -> GltfMesh &
    {
        return m_Meshes[i];
    }

  private:
    RegionAlloc *m_Alloc;

    Span<char> m_JsonChunk;
    Span<char> m_BinChunk;

    InlineVec<GltfBuffer, 1> m_Buffers;
    InlineVec<GltfImage, 12> m_Images;
    InlineVec<GltfBufferView, 6> m_BufferViews;
    InlineVec<GltfAccessor, 6> m_Accessors;
    InlineVec<GltfMesh, 1> m_Meshes;
};

} // namespace nyla