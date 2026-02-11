#include "nyla/formats/gltf/gltf_parser.h"
#include "nyla/commons/align.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/word.h"
#include "nyla/formats/json/json_parser.h"
#include "nyla/formats/json/json_value.h"
#include <cstdint>

namespace nyla
{

auto GlbChunkParser::Parse(std::span<char> &jsonChunk, std::span<char> &binChunk) -> bool
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

auto GltfParser::FindAttributeAccessor(std::span<GltfMeshPrimitiveAttribute> attributes, std::string_view attributeName,
                                       GltfAccessor &out) -> bool
{
    for (auto attribute : attributes)
    {
        if (attribute.name == attributeName)
        {
            out = GetAccessor(attribute.accessor);
            return true;
        }
    }
    return false;
}

auto GltfParser::Parse() -> bool
{
    JsonParser jsonParser;
    jsonParser.Init(m_Alloc, m_JsonChunk.data(), m_JsonChunk.size_bytes());
    JsonValue *jsonChunk = jsonParser.ParseNext();

    {
        JsonValue *images = jsonChunk->Array("images");
        for (auto it = images->begin(), end = images->end(); it != end; ++it)
        {
            auto &image = m_Images.emplace_back(GltfImage{});
            image.uri = it->String("uri");
            image.mimeType = it->String("mimeType");
            image.name = it->String("name");
        }
    }

    {
        JsonValue *buffers = jsonChunk->Array("buffers");
        for (auto it = buffers->begin(), end = buffers->end(); it != end; ++it)
        {
            auto &buffer = m_Buffers.emplace_back(GltfBuffer{});
            buffer.byteLength = it->DWord("byteLength");
        }
    }

    {
        JsonValue *bufferViews = jsonChunk->Array("bufferViews");
        for (auto it = bufferViews->begin(), end = bufferViews->end(); it != end; ++it)
        {
            auto &bufferView = m_BufferViews.emplace_back(GltfBufferView{});
            bufferView.buffer = it->DWord("buffer");

            if (!it->TryDWord("byteOffset", bufferView.byteOffset))
                bufferView.byteOffset = 0;

            bufferView.byteLength = it->DWord("byteLength");
        }
    }

    {
        JsonValue *accessors = jsonChunk->Array("accessors");
        for (auto it = accessors->begin(), end = accessors->end(); it != end; ++it)
        {
            auto &accessor = m_Accessors.emplace_back(GltfAccessor{});
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

            std::string_view accessorType = it->String("type");
            if (accessorType == "SCALAR")
                accessor.type = GltfAccessorType::SCALAR;
            else if (accessorType == "VEC2")
                accessor.type = GltfAccessorType::VEC2;
            else if (accessorType == "VEC3")
                accessor.type = GltfAccessorType::VEC3;
            else if (accessorType == "VEC4")
                accessor.type = GltfAccessorType::VEC4;
            else if (accessorType == "MAT2")
                accessor.type = GltfAccessorType::MAT2;
            else if (accessorType == "MAT3")
                accessor.type = GltfAccessorType::MAT3;
            else if (accessorType == "MAT4")
                accessor.type = GltfAccessorType::MAT4;
            else
                NYLA_ASSERT(false);
        }
    }

    {
        JsonValue *meshes = jsonChunk->Array("meshes");
        for (auto it = meshes->begin(), end = meshes->end(); it != end; ++it)
        {
            auto &mesh = m_Meshes.emplace_back(GltfMesh{});

            if (!it->TryString("name", mesh.name))
                mesh.name = {};

            JsonValue *primitivesJson = it->Array("primitives");
            uint32_t primitivesCount = primitivesJson->GetCount();
            mesh.primitives = m_Alloc->PushArr<GltfMeshPrimitive>(primitivesCount);

            JsonValue *primitiveJson = primitivesJson->GetFront();
            for (uint32_t i = 0; i < primitivesCount; ++i, primitiveJson = primitiveJson->Skip())
            {
                auto &primitive = mesh.primitives[i] = GltfMeshPrimitive{};

                primitive.indices = primitiveJson->DWord("indices");
                primitive.material = primitiveJson->DWord("material");

                JsonValue *attributesJson = primitiveJson->Object("attributes");
                uint32_t attributesCount = attributesJson->GetCount();

                primitive.attributes = m_Alloc->PushArr<GltfMeshPrimitiveAttribute>(attributesCount);

                JsonValue *attributeJson = attributesJson->GetFront();
                for (uint32_t j = 0; j < attributesCount; ++j)
                {
                    auto &attribute = primitive.attributes[j] = GltfMeshPrimitiveAttribute{};

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

} // namespace nyla