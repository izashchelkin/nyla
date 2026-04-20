#include <cstdint>

#include "nyla/commons/byteparser.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/json_parser.h"
#include "nyla/commons/json_value.h"
#include "nyla/commons/stringparser.h"

namespace nyla
{

namespace JsonParser
{

namespace
{

auto PushOut(json_parser &self, const json_value &value) -> json_value *
{
    ASSERT(self.outSize > 0);

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
        ASSERT(ByteParser::Read(self) == 'u');
        ASSERT(ByteParser::Read(self) == 'l');
        ASSERT(ByteParser::Read(self) == 'l');

        return PushOut(self, json_value{});
    }

    case 't': {
        ASSERT(ByteParser::Read(self) == 'r');
        ASSERT(ByteParser::Read(self) == 'u');
        ASSERT(ByteParser::Read(self) == 'e');

        json_value val;
        JsonValue::SetValue(val, true);

        return PushOut(self, val);
    }

    case 'f': {
        ASSERT(ByteParser::Read(self) == 'a');
        ASSERT(ByteParser::Read(self) == 'l');
        ASSERT(ByteParser::Read(self) == 's');
        ASSERT(ByteParser::Read(self) == 'e');

        json_value val;
        JsonValue::SetValue(val, false);

        return PushOut(self, val);
    }

    default: {
        ASSERT(false);
        return nullptr;
    }
    }
}

auto ParseNumber(json_parser &self) -> json_value *
{
    double doubleVal;
    int64_t longVal;

    json_value val;
    switch (StringParser::ParseDecimal(self, doubleVal, longVal))
    {
    case StringParser::ParseNumberResult::Double: {
        JsonValue::SetValue(val, doubleVal);
        break;
    }
    case StringParser::ParseNumberResult::Long: {
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

    uint8_t prevch = 0;
    while (ByteParser::HasNext(self))
    {
        const uint8_t ch = ByteParser::Read(self);
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

        StringParser::SkipWhitespace(self);
        const uint8_t ch = ByteParser::Read(self);
        if (ch == ']')
            break;

        ASSERT(ch == ',');
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
        ASSERT(key->tag == json_tag::String);

        StringParser::SkipWhitespace(self);
        ASSERT(ByteParser::Read(self) == ':');

        json_value *val = ParseNext(self);

        StringParser::SkipWhitespace(self);
        const uint8_t ch = ByteParser::Read(self);
        if (ch == '}')
            break;

        ASSERT(ch == ',');
    }

    json_value *end = PushOut(self, json_value());

    JsonValue::SetValue(*begin, json_tag::ObjectBegin, count, end);
    JsonValue::SetValue(*end, json_tag::ObjectEnd);

    return begin;
}

} // namespace

auto ParseNext(json_parser &self) -> json_value *
{
    StringParser::SkipWhitespace(self);

    uint8_t ch = ByteParser::Peek(self);
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

    ASSERT(false);
    return nullptr;
}

} // namespace JsonParser

} // namespace nyla