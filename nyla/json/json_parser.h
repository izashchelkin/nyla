#include "nyla/commons/assert.h"
#include "nyla/commons/log.h"
#include "nyla/commons/memory/region_alloc.h"
#include <cstdint>
#include <memory>
#include <string_view>

namespace nyla
{

class JsonParser
{
  public:
    struct Value
    {
        enum class Tag
        {
            Null,
            Bool,
            Integer,
            Float,
            String,
            ArrayBegin,
            ObjectBegin,
        };

        Tag tag;
        union {
            bool b;
            uint64_t i;
            double f;
            std::string_view s;
            uint32_t len;
        };
    };

    JsonParser(RegionAlloc &alloc, const char *base, uint32_t size) : m_Alloc{alloc}, m_At{base}, m_Left{size}
    {
    }

    auto ParseNext() -> Value *;

  private:
    auto Peek() -> char;
    void Advance();
    auto Pop() -> char;

    auto ParseNumber() -> Value *;
    auto ParseLiteral() -> Value *;
    auto ParseString() -> Value *;
    auto ParseArray() -> Value *;
    auto ParseObject() -> Value *;
    void SkipWhitespace();

    RegionAlloc &m_Alloc;
    const char *m_At;
    uint32_t m_Left;
};

} // namespace nyla