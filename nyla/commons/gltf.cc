#include "nyla/commons/gltf.h"

#include <cstdint>

#include "nyla/commons/json_parser.h"
#include "nyla/commons/json_value.h"
#include "nyla/commons/region_alloc.h"

namespace nyla
{

#if 0
auto GlbChunkParser::Parse(Span<char> &jsonChunk, Span<char> &binChunk) -> bool
{
    if (PopDWord() != DWord("glTF"))
        return false;

    const uint32_t version = PopDWord();
    const uint32_t length = PopDWord();

    //

    const uint32_t jsonChunkLength = PopDWord();

    if (PopDWord() != DWord("JSON"))
        return false;

    jsonChunk = {(char *)m_At, jsonChunkLength};

    m_At = (char *)m_At + jsonChunkLength;
    m_At = (char *)m_Base + AlignedUp<uint32_t>((char *)m_At - (char *)m_Base, 4);

    const uint32_t binChunkLength = PopDWord();
    if (PopDWord() != DWord("BIN\0"))
        return false;

    binChunk = {(char *)m_At, binChunkLength};

    return true;
}
#endif

namespace GltfParser
{

auto FindAttributeAccessor(gltf_parser &self, span<gltf_mesh_primitive_attribute> attributes, byteview attributeName,
                           gltf_accessor &out) -> bool
{
    for (auto attribute : attributes)
    {
        if (Span::Eq(attribute.name, attributeName))
        {
            out = self.accessors[attribute.accessor];
            return true;
        }
    }
    return false;
}

auto Parse(gltf_parser &self, region_alloc &alloc) -> bool
{
    json_parser jsonParser;
    JsonParser::Init(jsonParser, self.jsonChunk, RegionAlloc::AllocArray<json_value>(alloc, 1024));

    json_value &jsonChunk = *JsonParser::ParseNext(jsonParser);

    {
        json_value *images = JsonValue::Array(jsonChunk, "images"_s);
        self.images = RegionAlloc::AllocArray<gltf_image>(alloc, JsonValue::GetCount(*images));

        uint64_t i = 0;
        for (auto it = images->begin(), end = images->end(); it != end; ++it, ++i)
        {
            gltf_image &image = self.images[i];
            image.uri = JsonValue::String(*it, "uri"_s);
            image.mimeType = JsonValue::String(*it, "mimeType"_s);
            image.name = JsonValue::String(*it, "name"_s);
        }
    }

    {
        json_value *buffers = JsonValue::Array(jsonChunk, "buffers"_s);
        self.buffers = RegionAlloc::AllocArray<gltf_buffer>(alloc, JsonValue::GetCount(*buffers));

        uint64_t i = 0;
        for (auto it = buffers->begin(), end = buffers->end(); it != end; ++it, ++i)
        {
            auto &buffer = self.buffers[i];
            buffer.byteLength = JsonValue::DWord(*it, "byteLength"_s);
        }
    }

    {
        json_value *bufferViews = JsonValue::Array(jsonChunk, "bufferViews"_s);
        self.bufferViews = RegionAlloc::AllocArray<gltf_buffer_view>(alloc, JsonValue::GetCount(*bufferViews));

        uint64_t i = 0;
        for (auto it = bufferViews->begin(), end = bufferViews->end(); it != end; ++it, ++i)
        {
            auto &bufferView = self.bufferViews[i];
            bufferView.buffer = JsonValue::DWord(*it, "buffer"_s);

            if (!JsonValue::TryDWord(*it, "byteOffset"_s, bufferView.byteOffset))
                bufferView.byteOffset = 0;

            bufferView.byteLength = JsonValue::DWord(*it, "byteLength"_s);
        }
    }

    {
        json_value *accessors = JsonValue::Array(jsonChunk, "accessors"_s);
        self.accessors = RegionAlloc::AllocArray<gltf_accessor>(alloc, JsonValue::GetCount(*accessors));

        uint64_t i = 0;
        for (auto it = accessors->begin(), end = accessors->end(); it != end; ++it, ++i)
        {
            auto &accessor = self.accessors[i];
            accessor.bufferView = JsonValue::DWord(*it, "bufferView"_s);
            accessor.count = JsonValue::DWord(*it, "count"_s);

            if (!JsonValue::TryDWord(*it, "byteOffset"_s, accessor.byteOffset))
                accessor.byteOffset = 0;

            accessor.componentType = (gltf_accessor_component_type)JsonValue::DWord(*it, "componentType"_s);
            switch (accessor.componentType)
            {
            case nyla::gltf_accessor_component_type::BYTE:
            case nyla::gltf_accessor_component_type::UNSIGNED_BYTE:
            case nyla::gltf_accessor_component_type::SHORT:
            case nyla::gltf_accessor_component_type::UNSIGNED_SHORT:
            case nyla::gltf_accessor_component_type::UNSIGNED_INT:
            case nyla::gltf_accessor_component_type::FLOAT:
                break;

            default:
                NYLA_ASSERT(false);
            };

            byteview accessorType = JsonValue::String(*it, "type"_s);
            if (Span::Eq(accessorType, "SCALAR"_s))
                accessor.type = gltf_accessor_type::SCALAR;
            else if (Span::Eq(accessorType, "VEC2"_s))
                accessor.type = gltf_accessor_type::VEC2;
            else if (Span::Eq(accessorType, "VEC3"_s))
                accessor.type = gltf_accessor_type::VEC3;
            else if (Span::Eq(accessorType, "VEC4"_s))
                accessor.type = gltf_accessor_type::VEC4;
            else if (Span::Eq(accessorType, "MAT2"_s))
                accessor.type = gltf_accessor_type::MAT2;
            else if (Span::Eq(accessorType, "MAT3"_s))
                accessor.type = gltf_accessor_type::MAT3;
            else if (Span::Eq(accessorType, "MAT4"_s))
                accessor.type = gltf_accessor_type::MAT4;
            else
                NYLA_ASSERT(false);
        }
    }

    {
        json_value *meshes = JsonValue::Array(jsonChunk, "meshes"_s);
        self.meshes = RegionAlloc::AllocArray<gltf_mesh>(alloc, JsonValue::GetCount(*meshes));

        uint64_t i = 0;
        for (auto it = meshes->begin(), end = meshes->end(); it != end; ++it, ++i)
        {
            auto &mesh = self.meshes[i];

            if (!JsonValue::TryString(*it, "name"_s, mesh.name))
                mesh.name = {};

            json_value *primitivesJson = JsonValue::Array(*it, "primitives"_s);
            uint32_t primitivesCount = JsonValue::GetCount(*primitivesJson);
            mesh.primitives = RegionAlloc::AllocArray<gltf_mesh_primitive>(alloc, primitivesCount);

            json_value *primitiveJson = JsonValue::GetFront(*primitivesJson);
            for (uint32_t i = 0; i < primitivesCount; ++i, primitiveJson = JsonValue::GetNext(*primitiveJson))
            {
                auto &primitive = mesh.primitives[i] = gltf_mesh_primitive{};

                primitive.indices = JsonValue::DWord(*primitiveJson, "indices"_s);
                primitive.material = JsonValue::DWord(*primitiveJson, "material"_s);

                json_value *attributesJson = JsonValue::Object(*primitiveJson, "attributes"_s);
                uint32_t attributesCount = JsonValue::GetCount(*attributesJson);

                primitive.attributes = RegionAlloc::AllocArray<gltf_mesh_primitive_attribute>(alloc, attributesCount);

                json_value *attributeJson = JsonValue::GetFront(*attributesJson);
                for (uint32_t j = 0; j < attributesCount; ++j)
                {
                    auto &attribute = primitive.attributes[j] = gltf_mesh_primitive_attribute{};

                    attribute.name = JsonValue::String(*attributeJson);
                    attributeJson = JsonValue::GetNext(*attributeJson);

                    attribute.accessor = JsonValue::DWord(*attributeJson);
                    attributeJson = JsonValue::GetNext(*attributeJson);
                }
            }
        }
    }

    return true;
}

} // namespace GltfParser

} // namespace nyla