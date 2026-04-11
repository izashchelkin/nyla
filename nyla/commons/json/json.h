#pragma once

#include <cstdint>

#include "nyla/commons/byteparser.h"
#include "nyla/commons/region_alloc.h"

namespace nyla
{

class JsonValue;

class JsonParser : public ByteParser
{
  public:
    void Init(RegionAlloc *alloc, const char *base, uint64_t size)
    {
        m_Alloc = alloc;
        ByteParser::Init(base, size);
    }

    auto ParseNext() -> JsonValue *;

  private:
    auto ParseNumber() -> JsonValue *;
    auto ParseLiteral() -> JsonValue *;
    auto ParseString() -> JsonValue *;
    auto ParseArray() -> JsonValue *;
    auto ParseObject() -> JsonValue *;

    RegionAlloc *m_Alloc;
};

} // namespace nyla