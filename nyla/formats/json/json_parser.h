#pragma once

#include "nyla/commons/byteparser.h"
#include "nyla/commons/memory/region_alloc.h"
#include <cstdint>

namespace nyla
{

class JsonValue;

class JsonParser : public ByteParser
{
  public:
    void Init(RegionAlloc *alloc, const char *base, uint32_t size)
    {
        m_Alloc = alloc;
        m_At = base;
        m_Left = size;
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