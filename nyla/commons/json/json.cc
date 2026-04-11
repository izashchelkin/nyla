#include <cstdint>

#include "nyla/commons/byteparser.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/json/json.h"
#include "nyla/commons/json/json_value.h"

namespace nyla
{

namespace JsonParser
{

namespace
{

auto PushOut(json_parser &self, const json_value &value) -> json_value *
{
    NYLA_ASSERT(self.outSize > 0);

    json_value *ret = self.out;
    *ret = value;

    ++self.out;
    --self.outSize;

    return ret;
}

auto ParseLiteral(json_parser &self) -> json_value *
{
    switch (ByteParser::Read(self))
    {
    case 'n': {
        NYLA_ASSERT(ByteParser::Read(self) == 'u');
        NYLA_ASSERT(ByteParser::Read(self) == 'l');
        NYLA_ASSERT(ByteParser::Read(self) == 'l');

        return PushOut(self, json_value{});
    }

    case 't': {
        NYLA_ASSERT(ByteParser::Read(self) == 'r');
        NYLA_ASSERT(ByteParser::Read(self) == 'u');
        NYLA_ASSERT(ByteParser::Read(self) == 'e');

        json_value val;
        JsonValue::SetValue(val, true);

        return PushOut(self, val);
    }

    case 'f': {
        NYLA_ASSERT(ByteParser::Read(self) == 'a');
        NYLA_ASSERT(ByteParser::Read(self) == 'l');
        NYLA_ASSERT(ByteParser::Read(self) == 's');
        NYLA_ASSERT(ByteParser::Read(self) == 'e');

        json_value val;
        JsonValue::SetValue(val, false);

        return PushOut(self, val);
    }

    default: {
        NYLA_ASSERT(false);
        return nullptr;
    }
    }
}

auto ParseNumber(json_parser &self) -> json_value *
{
    double doubleVal;
    int64_t longVal;

    json_value val;
    switch (ByteParser::ParseDecimal(self, doubleVal, longVal))
    {
    case ByteParser::ParseNumberResult::Double: {
        JsonValue::SetValue(val, doubleVal);
        break;
    }
    case ByteParser::ParseNumberResult::Long: {
        JsonValue::SetValue(val, longVal);
        break;
    }
    }

    return PushOut(self, val);
}

auto ParseString(json_parser &self) -> json_value *
{
    const uint8_t *base = self.at;
    uint64_t count = 0;

    char prevch = 0;
    while (ByteParser::HasNext(self))
    {
        const char ch = ByteParser::Read(self);
        if (ch == '"' /*  && prevch != '\\' */)
            break;

        prevch = ch;

        ++count;
    }

    json_value val;
    JsonValue::SetValue(val, byteview{base, count});

    return PushOut(self, val);
}

auto ParseArray(json_parser &self) -> json_value *
{
    json_value *begin = PushOut(self, json_value());
    int32_t count = 0;

    for (;;)
    {
        if (ByteParser::Peek(self) == ']')
        {
            ByteParser::Advance(self);
            break;
        }

        ++count;

        json_value *elem = ParseNext(self);

        ByteParser::SkipWhitespace(self);
        const char ch = ByteParser::Read(self);
        if (ch == ']')
            break;

        NYLA_ASSERT(ch == ',');
    }

    json_value *end = PushOut(self, json_value());

    JsonValue::SetValue(*begin, json_tag::ArrayBegin, count, end);
    JsonValue::SetValue(*end, json_tag::ArrayEnd);

    return begin;
}

auto ParseObject(json_parser &self) -> json_value *
{
    auto *begin = PushOut(self, json_value{});
    uint32_t count = 0;

    for (;;)
    {
        if (ByteParser::Peek(self) == '}')
        {
            ByteParser::Advance(self);
            break;
        }

        ++count;

        json_value *key = ParseNext(self);
        NYLA_ASSERT(key->tag == json_tag::String);

        ByteParser::SkipWhitespace(self);
        NYLA_ASSERT(ByteParser::Read(self) == ':');

        json_value *val = ParseNext(self);

        ByteParser::SkipWhitespace(self);
        const char ch = ByteParser::Read(self);
        if (ch == '}')
            break;

        NYLA_ASSERT(ch == ',');
    }

    json_value *end = PushOut(self, json_value());

    JsonValue::SetValue(*begin, json_tag::ObjectBegin, count, end);
    JsonValue::SetValue(*end, json_tag::ObjectEnd);

    return begin;
}

} // namespace

auto ParseNext(json_parser &self) -> json_value *
{
    ByteParser::SkipWhitespace(self);

    char ch = ByteParser::Peek(self);
    if (IsNumber(ch) || ch == '-')
        return ParseNumber(self);
    if (IsAlpha(ch))
        return ParseLiteral(self);

    ByteParser::Advance(self);
    if (ch == '"')
        return ParseString(self);
    if (ch == '[')
        return ParseArray(self);
    if (ch == '{')
        return ParseObject(self);

    NYLA_ASSERT(false);
    return nullptr;
}

} // namespace JsonParser

} // namespace nyla