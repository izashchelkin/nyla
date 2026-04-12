#pragma once

#include <cstdint>

#include "nyla/commons/region_alloc.h"
#include "nyla/commons/span.h"

namespace nyla
{

struct gltf_buffer
{
    uint32_t byteLength;
};

struct gltf_image
{
    byteview uri;
    byteview mimeType;
    byteview name;
};

struct gltf_buffer_view
{
    uint32_t buffer;
    uint32_t byteOffset;
    uint32_t byteLength;
};

enum class gltf_accessor_component_type
{
    BYTE = 5120,
    UNSIGNED_BYTE = 5121,
    SHORT = 5122,
    UNSIGNED_SHORT = 5123,
    UNSIGNED_INT = 5125,
    FLOAT = 5126,
};

INLINE auto GetGltfAccessorComponentSize(gltf_accessor_component_type componentType) -> uint32_t
{
    switch (componentType)
    {
    case gltf_accessor_component_type::BYTE:
    case gltf_accessor_component_type::UNSIGNED_BYTE:
        return 1;

    case gltf_accessor_component_type::SHORT:
    case gltf_accessor_component_type::UNSIGNED_SHORT:
        return 2;

    case gltf_accessor_component_type::UNSIGNED_INT:
    case gltf_accessor_component_type::FLOAT:
        return 4;
    }
}

enum class gltf_accessor_type
{
    SCALAR,
    VEC2,
    VEC3,
    VEC4,
    MAT2,
    MAT3,
    MAT4,
};

INLINE auto GetGltfAccessorComponentCount(gltf_accessor_type accessorType) -> uint32_t
{
    switch (accessorType)
    {
    case gltf_accessor_type::SCALAR:
        return 1;

    case gltf_accessor_type::VEC2:
        return 2;

    case gltf_accessor_type::VEC3:
        return 3;

    case gltf_accessor_type::VEC4:
    case gltf_accessor_type::MAT2:
        return 4;

    case gltf_accessor_type::MAT3:
        return 9;

    case gltf_accessor_type::MAT4:
        return 16;
    }
}

struct gltf_accessor
{
    uint32_t bufferView;
    uint32_t byteOffset;
    gltf_accessor_component_type componentType;
    uint32_t count;
    gltf_accessor_type type;
};

INLINE auto GetGltfAccessorSize(const gltf_accessor &accessor)
{
    return GetGltfAccessorComponentCount(accessor.type) * GetGltfAccessorComponentSize(accessor.componentType);
}

struct gltf_mesh_primitive_attribute
{
    byteview name;
    uint32_t accessor;
};

struct gltf_mesh_primitive
{
    span<gltf_mesh_primitive_attribute> attributes;
    uint32_t mode;
    uint32_t indices;
    uint32_t material;
};

struct gltf_mesh
{
    byteview name;
    span<gltf_mesh_primitive> primitives;
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

struct gltf_parser
{
    byteview jsonChunk;
    byteview binChunk;

    uint8_t *out;
    uint64_t outSize;

    span<gltf_buffer_view> bufferViews;
    span<gltf_buffer> buffers;
    span<gltf_accessor> accessors;
    span<gltf_image> images;
    span<gltf_mesh> meshes;
};

namespace GltfParser
{

auto Parse(gltf_parser &self, region_alloc &alloc) -> bool;

auto FindAttributeAccessor(gltf_parser &self, span<gltf_mesh_primitive_attribute> attributes, byteview attributeName,
                           gltf_accessor &out) -> bool;

} // namespace GltfParser

} // namespace nyla