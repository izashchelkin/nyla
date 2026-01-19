#include "nyla/commons/assert.h"
#include "nyla/commons/memory/region_alloc.h"
#include <cstdint>
#include <string_view>

namespace nyla
{

struct JsonNode
{
    enum class Tag
    {
        Integer,
        Float,
        String,
        Object,
        Array,
    };

    Tag tag;
    union {
        uint64_t i64;
        double f64;
        std::string_view str;
    };
};

namespace
{

auto Peek(std::string_view str)
{
    return str.at(0);
}

auto Pop(std::string_view &str)
{
    const char ch = str.at(0);
    str = str.substr(1);
    return ch;
}

auto IsNumber(unsigned char ch) -> bool
{
    return ch >= '0' && ch <= '9';
}

auto IsAlpha(unsigned char ch) -> bool
{
    return (ch >= 'a' && ch < 'z') || (ch >= 'A' && ch < 'Z');
}

auto ParseNumber(std::string_view &str) -> JsonNode
{
    uint64_t num = 0;

    while (!str.empty() && IsNumber(Peek(str)))
    {
        num *= 10;
        num += str.at(0) - '0';
        Pop(str);
    }

    return {
        .tag = JsonNode::Tag::Integer,
        .i64 = num,
    };
}

auto ParseString(std::string_view &str) -> JsonNode
{
    const char *base = str.data();
    uint32_t count = 0;

    while (!str.empty() && Pop(str) != '"')
        ++count;

    return {
        .tag = JsonNode::Tag::String,
        .str = std::string_view{base, count},
    };
}

auto SkipWhitespace(std::string_view &str)
{
    while (Peek(str) == ' ')
        Pop(str);
}

} // namespace

auto JsonUnmarshal(RegionAlloc &alloc, std::string_view &str) -> JsonNode *
{
    char ch = Peek(str);

    SkipWhitespace(str);

    if (IsNumber(ch))
        return alloc.Push(ParseNumber(str));

    Pop(str);
    if (ch == '"')
    {
        str = str.substr(1);
        return alloc.Push(ParseString(str));
    }

    if (ch == '{')
    {
        auto *count = alloc.Push<uint32_t>(0);

        for (;;)
        {
            JsonNode *key = JsonUnmarshal(alloc, str);
            NYLA_ASSERT(key->tag == JsonNode::Tag::String);

            SkipWhitespace(str);
            NYLA_ASSERT(Pop(str) == ':');
            SkipWhitespace(str);

            JsonNode *val = JsonUnmarshal(alloc, str);

            SkipWhitespace(str);

            ch = Pop(str);
            if (ch == '}')
                break;

            NYLA_ASSERT(ch == ',');
        }
    }
}

} // namespace nyla