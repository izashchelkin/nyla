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

    JsonParser jsonParser{m_Alloc, (const char *)m_At, jsonChunkLength};
    JsonValue *jsonChunk = jsonParser.ParseNext();

    LogJsonValue(jsonChunk);

    {
        JsonValue *buffers = jsonChunk->Array("buffers");
        auto end = buffers->end();
        for (auto it = buffers->begin(); it != end; ++it)
        {
            auto &buffer = m_Buffers.emplace_back(GltfBuffer{});
            buffer.byteLength = (*it)->Integer("byteLength");
        }
    }

    {
        JsonValue *bufferViews = jsonChunk->Array("bufferViews");
        auto end = bufferViews->end();
        for (auto it = bufferViews->begin(); it != end; ++it)
        {
            auto &bufferView = m_BufferViews.emplace_back(GltfBufferView{});
            bufferView.buffer = (*it)->Integer("buffer");
            bufferView.byteOffset = (*it)->Integer("byteOffset");
            bufferView.byteLength = (*it)->Integer("byteLength");
        }
    }

    {
        JsonValue *accessors = jsonChunk->Array("accessors");
        auto end = accessors->end();
        for (auto it = accessors->begin(); it != end; ++it)
        {
            auto &accessor = m_Accessors.emplace_back(GltfAccessor{});
            accessor.bufferView = (*it)->Integer("bufferView");

            uint64_t byteOffset = 0;
            (*it)->TryInteger("byteOffset", byteOffset);
            accessor.byteOffset = byteOffset;

            accessor.componentType = (*it)->Integer("componentType");
            accessor.count = (*it)->Integer("count");
            accessor.type = (*it)->String("type");
        }
    }

    //

    m_At = (char *)m_At + jsonChunkLength;
    m_At = (char *)m_Base + AlignedUp<uint32_t>((char *)m_At - (char *)m_Base, 4);

    const uint32_t binChunkLength = PopDWord();

    uint32_t header = PopDWord();

    if (header != DWord("BIN\0"))
        return false;

    return true;
}

} // namespace nyla