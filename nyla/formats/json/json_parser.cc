#include "nyla/formats/json/json_parser.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/memory/region_alloc.h"
#include "nyla/formats/json/json_value.h"
#include <cstdint>

namespace nyla
{

namespace
{

auto IsNumber(unsigned char ch) -> bool
{
    return ch >= '0' && ch <= '9';
}

auto IsAlpha(unsigned char ch) -> bool
{
    return (ch >= 'a' && ch < 'z') || (ch >= 'A' && ch < 'Z');
}

} // namespace

void JsonParser::Init(RegionAlloc *alloc, const char *base, uint32_t size)
{
    m_Alloc = alloc;
    m_At = base;
    m_Left = size;
}

auto JsonParser::ParseNext() -> JsonValue *
{
    SkipWhitespace();

    char ch = Peek();
    if (IsNumber(ch) || ch == '-')
        return ParseNumber();
    if (IsAlpha(ch))
        return ParseLiteral();

    Advance();
    if (ch == '"')
        return ParseString();
    if (ch == '[')
        return ParseArray();
    if (ch == '{')
        return ParseObject();

    NYLA_ASSERT(false);
}

auto JsonParser::ParseLiteral() -> JsonValue *
{
    switch (Pop())
    {
    case 'n': {
        NYLA_ASSERT(Pop() == 'u');
        NYLA_ASSERT(Pop() == 'l');
        NYLA_ASSERT(Pop() == 'l');
        return m_Alloc->Push(JsonValue{});
    }

    case 't': {
        NYLA_ASSERT(Pop() == 'r');
        NYLA_ASSERT(Pop() == 'u');
        NYLA_ASSERT(Pop() == 'e');

        JsonValue val;
        val.SetValue(true);

        return m_Alloc->Push(val);
    }

    case 'f': {
        NYLA_ASSERT(Pop() == 'a');
        NYLA_ASSERT(Pop() == 'l');
        NYLA_ASSERT(Pop() == 's');
        NYLA_ASSERT(Pop() == 'e');

        JsonValue val;
        val.SetValue(false);

        return m_Alloc->Push(val);
    }

    default: {
        NYLA_ASSERT(false);
        return nullptr;
    }
    }
}

auto JsonParser::ParseNumber() -> JsonValue *
{
    uint32_t sign = 1;
    if (Peek() == '-')
    {
        sign = -1;
        Advance();
    }

    uint64_t integer = 0;
    uint64_t fraction = 0;
    uint64_t fractionCount = 0;

    while (m_Left > 0 && IsNumber(Peek()))
    {
        integer *= 10;
        integer += Pop() - '0';
    }

    if (Peek() == '.')
    {
        Advance();
        while (m_Left > 0 && IsNumber(Peek()))
        {
            ++fractionCount;
            fraction *= 10;
            fraction += Pop() - '0';
        }

        auto f = static_cast<double>(fraction);
        for (uint32_t i = 0; i < fractionCount; ++i)
            f /= 10.0;

        f += static_cast<double>(integer);

        JsonValue val;
        val.SetValue(static_cast<double>(sign * f));

        return m_Alloc->Push(val);
    }
    else
    {
        JsonValue val;
        val.SetValue(static_cast<uint64_t>(sign * integer));

        return m_Alloc->Push(val);
    }
}

auto JsonParser::ParseString() -> JsonValue *
{
    const char *base = m_At;
    uint32_t count = 0;

    char prevch = 0;
    while (m_Left > 0)
    {
        const char ch = Pop();
        if (ch == '"' /*  && prevch != '\\' */)
            break;

        prevch = ch;

        ++count;
    }

    std::string_view s{base, count};

    JsonValue val;
    val.SetValue(base, count);

    return m_Alloc->Push(val);
}

auto JsonParser::ParseArray() -> JsonValue *
{
    JsonValue *begin = m_Alloc->Push(JsonValue());
    int32_t count = 0;

    for (;;)
    {
        if (Peek() == ']')
        {
            Advance();
            break;
        }

        ++count;

        JsonValue *elem = ParseNext();

        SkipWhitespace();
        const char ch = Pop();
        if (ch == ']')
            break;

        NYLA_ASSERT(ch == ',');
    }

    JsonValue *end = m_Alloc->Push(JsonValue());

    begin->SetValue(JsonTag::ArrayBegin, count, end);
    end->SetValue(JsonTag::ArrayEnd);

    return begin;
}

auto JsonParser::ParseObject() -> JsonValue *
{
    auto *begin = m_Alloc->Push<JsonValue>();
    uint32_t count = 0;

    for (;;)
    {
        if (Peek() == '}')
        {
            Advance();
            break;
        }

        ++count;

        JsonValue *key = ParseNext();
        NYLA_ASSERT(key->GetTag() == JsonTag::String);

        SkipWhitespace();
        NYLA_ASSERT(Pop() == ':');

        JsonValue *val = ParseNext();

        SkipWhitespace();
        const char ch = Pop();
        if (ch == '}')
            break;

        NYLA_ASSERT(ch == ',');
    }

    auto *end = m_Alloc->Push<JsonValue>();

    begin->SetValue(JsonTag::ObjectBegin, count, end);
    end->SetValue(JsonTag::ObjectEnd);

    return begin;
}

auto JsonParser::Peek() -> char
{
    return *m_At;
}

void JsonParser::Advance()
{
    ++m_At;
    --m_Left;
}

auto JsonParser::Pop() -> char
{
    const char ret = Peek();
    Advance();
    return ret;
}

void JsonParser::SkipWhitespace()
{
    for (;;)
    {
        const char ch = Peek();
        if (ch == ' ' || ch == '\n' || ch == '\t')
            Advance();
        else
            break;
    }
}

} // namespace nyla