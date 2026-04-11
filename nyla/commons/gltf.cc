#include "nyla/commons/gltf.h"

#include <cstdint>

#include "nyla/commons/align.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/json_parser.h"
#include "nyla/commons/json_value.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/word.h"

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
            out = GetAccessor(attribute.accessor);
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
        for (auto it = images->begin(), end = images->end(); it != end; ++it)
        {
            auto &image = m_Images.PushBack(GltfImage{});
            image.uri = it->String("uri");
            image.mimeType = it->String("mimeType");
            image.name = it->String("name");
        }
    }

    {
        json_value *buffers = JsonValue::Array(jsonChunk, "buffers"_s);
        for (auto it = buffers->begin(), end = buffers->end(); it != end; ++it)
        {
            auto &buffer = m_Buffers.PushBack(GltfBuffer{});
            buffer.byteLength = it->DWord("byteLength");
        }
    }

    {
        json_value *bufferViews = JsonValue::Array(jsonChunk, "bufferViews"_s);
        for (auto it = bufferViews->begin(), end = bufferViews->end(); it != end; ++it)
        {
            auto &bufferView = m_BufferViews.PushBack(GltfBufferView{});
            bufferView.buffer = it->DWord("buffer");

            if (!it->TryDWord("byteOffset", bufferView.byteOffset))
                bufferView.byteOffset = 0;

            bufferView.byteLength = it->DWord("byteLength");
        }
    }

    {
        json_value *accessors = JsonValue::Array(jsonChunk, "accessors"_s);
        for (auto it = accessors->begin(), end = accessors->end(); it != end; ++it)
        {
            auto &accessor = m_Accessors.PushBack(GltfAccessor{});
            accessor.bufferView = it->DWord("bufferView");
            accessor.count = it->DWord("count");

            if (!it->TryDWord("byteOffset", accessor.byteOffset))
                accessor.byteOffset = 0;

            accessor.componentType = GltfAccessorComponentType(it->DWord("componentType"));
            switch (accessor.componentType)
            {
            case GltfAccessorComponentType::BYTE:
            case GltfAccessorComponentType::UNSIGNED_BYTE:
            case GltfAccessorComponentType::SHORT:
            case GltfAccessorComponentType::UNSIGNED_SHORT:
            case GltfAccessorComponentType::UNSIGNED_INT:
            case GltfAccessorComponentType::FLOAT:
                break;

            default:
                NYLA_ASSERT(false);
            };

            Str accessorType = it->String("type");
            if (accessorType == "SCALAR")
                accessor.type = gltf_accessor_type::SCALAR;
            else if (accessorType == "VEC2")
                accessor.type = gltf_accessor_type::VEC2;
            else if (accessorType == "VEC3")
                accessor.type = gltf_accessor_type::VEC3;
            else if (accessorType == "VEC4")
                accessor.type = gltf_accessor_type::VEC4;
            else if (accessorType == "MAT2")
                accessor.type = gltf_accessor_type::MAT2;
            else if (accessorType == "MAT3")
                accessor.type = gltf_accessor_type::MAT3;
            else if (accessorType == "MAT4")
                accessor.type = gltf_accessor_type::MAT4;
            else
                NYLA_ASSERT(false);
        }
    }

    {
        json_value *meshes = jsonChunk->Array("meshes");
        for (auto it = meshes->begin(), end = meshes->end(); it != end; ++it)
        {
            auto &mesh = m_Meshes.PushBack(GltfMesh{});

            if (!it->TryString("name", mesh.name))
                mesh.name = {};

            JsonValue *primitivesJson = it->Array("primitives");
            uint32_t primitivesCount = primitivesJson->GetCount();
            mesh.primitives = m_Alloc->PushArr<GltfMeshPrimitive>(primitivesCount);

            JsonValue *primitiveJson = primitivesJson->GetFront();
            for (uint32_t i = 0; i < primitivesCount; ++i, primitiveJson = primitiveJson->Skip())
            {
                auto &primitive = mesh.primitives[i] = gltf_mesh_primitive{};

                primitive.indices = primitiveJson->DWord("indices");
                primitive.material = primitiveJson->DWord("material");

                JsonValue *attributesJson = primitiveJson->Object("attributes");
                uint32_t attributesCount = attributesJson->GetCount();

                primitive.attributes = m_Alloc->PushArr<GltfMeshPrimitiveAttribute>(attributesCount);

                JsonValue *attributeJson = attributesJson->GetFront();
                for (uint32_t j = 0; j < attributesCount; ++j)
                {
                    auto &attribute = primitive.attributes[j] = gltf_mesh_primitive_attribute{};

                    attribute.name = attributeJson->String();

                    attributeJson = attributeJson->Skip();

                    attribute.accessor = attributeJson->DWord();
                    attributeJson = attributeJson->Skip();
                }
            }
        }
    }

    return true;
}

} // namespace GltfParser

} // namespace nyla