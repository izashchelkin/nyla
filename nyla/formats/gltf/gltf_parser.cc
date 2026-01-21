#include "nyla/formats/gltf/gltf_parser.h"
#include "nyla/commons/word.h"
#include "nyla/formats/json/json_parser.h"
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

    const uint32_t jsonChunkLength = PopDWord();

    if (PopDWord() != DWord("JSON"))
        return false;

    JsonParser jsonParser{m_Alloc, (const char *)m_At, jsonChunkLength};
    jsonParser.ParseNext();

    return true;
}

} // namespace nyla