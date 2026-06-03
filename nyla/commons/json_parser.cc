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
    if (self.outSize == 0)
        return nullptr;

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
        if (!ByteParser::HasNext(self) || ByteParser::Read(self) != 'u' || !ByteParser::HasNext(self) ||
            ByteParser::Read(self) != 'l' || !ByteParser::HasNext(self) || ByteParser::Read(self) != 'l')
            return nullptr;

        json_value val;
        JsonValue::SetValue(val, json_tag::Null);
        return PushOut(self, val);
    }

    case 't': {
        if (!ByteParser::HasNext(self) || ByteParser::Read(self) != 'r' || !ByteParser::HasNext(self) ||
            ByteParser::Read(self) != 'u' || !ByteParser::HasNext(self) || ByteParser::Read(self) != 'e')
            return nullptr;

        json_value val;
        JsonValue::SetValue(val, true);

        return PushOut(self, val);
    }

    case 'f': {
        if (!ByteParser::HasNext(self) || ByteParser::Read(self) != 'a' || !ByteParser::HasNext(self) ||
            ByteParser::Read(self) != 'l' || !ByteParser::HasNext(self) || ByteParser::Read(self) != 's' ||
            !ByteParser::HasNext(self) || ByteParser::Read(self) != 'e')
            return nullptr;

        json_value val;
        JsonValue::SetValue(val, false);

        return PushOut(self, val);
    }

    default: {
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
    bool closed = false;

    while (ByteParser::HasNext(self))
    {
        const uint8_t ch = ByteParser::Read(self);
        if (ch == '"')
        {
            closed = true;
            break;
        }

        ++count;

        // Skip escaped character — backslash escapes the next char
        if (ch == '\\' && ByteParser::HasNext(self))
        {
            ByteParser::Read(self);
            ++count;
        }
    }

    if (!closed)
        return nullptr;

    json_value val;
    JsonValue::SetValue(val, byteview{base, count});

    return PushOut(self, val);
}

auto ParseArray(json_parser &self) -> json_value *
{
    json_value *begin = PushOut(self, json_value());
    if (!begin)
        return nullptr;

    int32_t count = 0;

    for (;;)
    {
        if (!ByteParser::HasNext(self))
            return nullptr;
        if (ByteParser::Peek(self) == ']')
        {
            ByteParser::Advance(self);
            break;
        }

        ++count;

        json_value *elem = ParseNext(self);
        if (!elem)
            return nullptr;

        StringParser::SkipWhitespace(self);
        if (!ByteParser::HasNext(self))
            return nullptr;
        const uint8_t ch = ByteParser::Read(self);
        if (ch == ']')
            break;

        if (ch != ',')
            return nullptr;
    }

    json_value *end = PushOut(self, json_value());
    if (!end)
        return nullptr;

    JsonValue::SetValue(*begin, json_tag::ArrayBegin, count, end);
    JsonValue::SetValue(*end, json_tag::ArrayEnd);

    return begin;
}

auto ParseObject(json_parser &self) -> json_value *
{
    auto *begin = PushOut(self, json_value{});
    if (!begin)
        return nullptr;

    uint32_t count = 0;

    for (;;)
    {
        if (!ByteParser::HasNext(self))
            return nullptr;
        if (ByteParser::Peek(self) == '}')
        {
            ByteParser::Advance(self);
            break;
        }

        ++count;

        json_value *key = ParseNext(self);
        if (!key || key->tag != json_tag::String)
            return nullptr;

        StringParser::SkipWhitespace(self);
        if (!ByteParser::HasNext(self) || ByteParser::Read(self) != ':')
            return nullptr;

        json_value *val = ParseNext(self);
        if (!val)
            return nullptr;

        StringParser::SkipWhitespace(self);
        if (!ByteParser::HasNext(self))
            return nullptr;
        const uint8_t ch = ByteParser::Read(self);
        if (ch == '}')
            break;

        if (ch != ',')
            return nullptr;
    }

    json_value *end = PushOut(self, json_value());
    if (!end)
        return nullptr;

    JsonValue::SetValue(*begin, json_tag::ObjectBegin, count, end);
    JsonValue::SetValue(*end, json_tag::ObjectEnd);

    return begin;
}

} // namespace

auto API ParseNext(json_parser &self) -> json_value *
{
    StringParser::SkipWhitespace(self);

    if (!ByteParser::HasNext(self))
        return nullptr;

    uint8_t ch = ByteParser::Peek(self);
    if (IsNumber(ch) || ch == '-')
        return ParseNumber(self);
    if (IsAlpha(ch))
        return ParseLiteral(self);

    if (!ByteParser::HasNext(self))
        return nullptr;
    ByteParser::Advance(self);
    if (ch == '"')
        return ParseString(self);
    if (ch == '[')
        return ParseArray(self);
    if (ch == '{')
        return ParseObject(self);

    return nullptr;
}

} // namespace JsonParser

} // namespace nyla
