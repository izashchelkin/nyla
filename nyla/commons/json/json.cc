#include "nyla/commons/json/json.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/json/json_value.h"
#include <cstdint>

namespace nyla
{

namespace
{
} // namespace

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
    double doubleVal;
    int64_t longVal;

    JsonValue val;
    switch (ParseDecimal(doubleVal, longVal))
    {
    case ByteParser::ParseNumberResult::Double: {
        val.SetValue(doubleVal);
        break;
    }
    case ByteParser::ParseNumberResult::Long: {
        val.SetValue(longVal);
        break;
    }
    }

    return m_Alloc->Push(val);
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

    Str s{base, count};

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

} // namespace nyla