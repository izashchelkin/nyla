#include "nyla/formats/gltf/gltf_parser.h"
#include "nyla/commons/align.h"
#include "nyla/commons/word.h"
#include "nyla/formats/json/json_parser.h"
#include "nyla/formats/json/json_value.h"
#include <cstdint>

namespace nyla
{

auto GltfParser::PopDWord() -> uint32_t
{
    const uint32_t ret = *(uint32_t *)m_At;
    m_At = (uint32_t *)m_At + 1;
    return ret;
}

void GltfParser::Init(RegionAlloc *alloc, void *data, uint32_t byteLength)
{
    m_Alloc = alloc;
    m_Base = data;
    m_At = data;
    m_BytesLeft = byteLength;
}

auto GltfParser::Parse() -> bool
{
    if (PopDWord() != DWord("glTF"))
        return false;

    const uint32_t version = PopDWord();
    const uint32_t length = PopDWord();

    //

    const uint32_t jsonChunkLength = PopDWord();

    if (PopDWord() != DWord("JSON"))
        return false;

    JsonParser jsonParser;
    jsonParser.Init(m_Alloc, (const char *)m_At, jsonChunkLength);
    JsonValue *jsonChunk = jsonParser.ParseNext();

    LogJsonValue(jsonChunk);

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

            if (!it->TryDWord("byteOffset", accessor.byteOffset))
                accessor.byteOffset = 0;

            accessor.componentType = it->DWord("componentType");
            accessor.count = it->DWord("count");
            accessor.type = it->String("type");
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

    //

    m_At = (char *)m_At + jsonChunkLength;
    m_At = (char *)m_Base + AlignedUp<uint32_t>((char *)m_At - (char *)m_Base, 4);

    const uint32_t binChunkLength = PopDWord();
    if (PopDWord() != DWord("BIN\0"))
        return false;

    m_BinChunk = {(char *)m_At, binChunkLength};

    return true;
}

} // namespace nyla