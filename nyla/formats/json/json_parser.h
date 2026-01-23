#pragma once

#include "nyla/commons/memory/region_alloc.h"
#include <cstdint>

namespace nyla
{

class JsonValue;

class JsonParser
{
  public:
    void Init(RegionAlloc *alloc, const char *base, uint32_t size);

    auto ParseNext() -> JsonValue *;

  private:
    auto Peek() -> char;
    void Advance();
    auto Pop() -> char;

    auto ParseNumber() -> JsonValue *;
    auto ParseLiteral() -> JsonValue *;
    auto ParseString() -> JsonValue *;
    auto ParseArray() -> JsonValue *;
    auto ParseObject() -> JsonValue *;
    void SkipWhitespace();

    RegionAlloc *m_Alloc;
    const char *m_At;
    uint32_t m_Left;
};

} // namespace nyla