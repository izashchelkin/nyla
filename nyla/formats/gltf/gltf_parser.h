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

struct GltfImage
{
    std::string_view uri;
    std::string_view mimeType;
    std::string_view name;
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

class GlbChunkParser
{
  public:
    void Init(void *data, uint32_t byteLength)
    {
        m_Base = (uint32_t *)data;
        m_At = m_Base;
        m_BytesLeft = byteLength;
    }

    auto PopDWord() -> uint32_t
    {
        const uint32_t ret = *(uint32_t *)m_At;
        m_At = (uint32_t *)m_At + 1;
        return ret;
    }

    auto Parse(std::span<char> &jsonChunk, std::span<char> &binChunk) -> bool;

  private:
    void *m_Base;
    void *m_At;
    uint64_t m_BytesLeft;
};

class GltfParser
{
  public:
    void Init(RegionAlloc *alloc, std::span<char> jsonChunk, std::span<char> binChunk)
    {
        m_Alloc = alloc;
        m_JsonChunk = jsonChunk;
        m_BinChunk = binChunk;
    }

    auto Parse() -> bool;
    auto PopDWord() -> uint32_t;
    auto FindAttributeAccessor(std::span<GltfMeshPrimitiveAttribute> attributes, std::string_view attributeName,
                               GltfAccessor &out) -> bool;

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

    auto GetAccessorData(const GltfAccessor &accessor) -> std::span<char>
    {
        return GetBufferViewData(GetBufferView(accessor.bufferView)).subspan(accessor.byteOffset);
    }

    auto GetAccessors() -> std::span<GltfAccessor>
    {
        return m_Accessors;
    }

    auto GetAccessor(uint32_t i) -> GltfAccessor &
    {
        return m_Accessors[i];
    }

    auto GetImages() -> std::span<GltfImage>
    {
        return m_Images;
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
    RegionAlloc *m_Alloc;

    std::span<char> m_JsonChunk;
    std::span<char> m_BinChunk;

    InlineVec<GltfBuffer, 1> m_Buffers;
    InlineVec<GltfImage, 12> m_Images;
    InlineVec<GltfBufferView, 6> m_BufferViews;
    InlineVec<GltfAccessor, 6> m_Accessors;
    InlineVec<GltfMesh, 1> m_Meshes;
};

} // namespace nyla