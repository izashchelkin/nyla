#include "nyla/formats/json/json_parser.h"
#include "nyla/formats/json/json_value.h"
#include <cstdint>
#include <string_view>

namespace nyla
{

namespace
{

auto JsonVisit(JsonValue *value, std::span<std::string_view> path, auto &&handler) -> bool
{
    if (path.empty())
        return handler(value);

    if (value->tag != JsonValue::Tag::ObjectBegin)
        return false;

    JsonValue *const end = value->col.end;
    ++value;

    for (; value != end; value += 2)
    {
        if (value->s == path.front())
            return JsonVisit(value + 1, path.subspan(1), handler);
    }

    return false;
}

} // namespace

auto JsonValue::begin() -> JsonValueIter
{
}

auto JsonValue::end() -> JsonValueIter
{
}

auto JsonValue::TryAny(std::span<std::string_view> path, JsonValue *&out) -> bool
{
    return JsonVisit(this, path, [&out](JsonValue *value) -> bool {
        out = value;
        return true;
    });
}

auto JsonValue::TryObject(std::span<std::string_view> path, JsonValue *&out) -> bool
{
    return JsonVisit(this, path, [&out](JsonValue *value) -> bool {
        if (value->tag == Tag::ObjectBegin)
        {
            out = value;
            return true;
        }
        else
            return false;
    });
}

auto JsonValue::TryArray(std::span<std::string_view> path, JsonValue *&out) -> bool
{
    return JsonVisit(this, path, [&out](JsonValue *value) -> bool {
        if (value->tag == Tag::ArrayBegin)
        {
            out = value;
            return true;
        }
        else
            return false;
    });
}

auto JsonValue::TryString(std::span<std::string_view> path, std::string_view &out) -> bool
{
    return JsonVisit(this, path, [&out](JsonValue *value) -> bool {
        if (value->tag == Tag::String)
        {
            out = value->s;
            return true;
        }
        else
            return false;
    });
}

auto JsonValue::TryInteger(std::span<std::string_view> path, uint64_t &out) -> bool
{
    return JsonVisit(this, path, [&out](JsonValue *value) -> bool {
        if (value->tag == Tag::Integer)
        {
            out = value->i;
            return true;
        }
        else
            return false;
    });
}

} // namespace nyla