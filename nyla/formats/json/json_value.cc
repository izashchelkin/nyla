#include "nyla/formats/json/json_value.h"
#include "nyla/commons/log.h"
#include "nyla/formats/json/json_parser.h"
#include <cinttypes>
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

    auto end = value->end();
    for (auto it = value->begin(); it != end; it += 2)
    {
        if ((*it)->s == path.front())
            return JsonVisit((*it) + 1, path.subspan(1), handler);
    }

    return false;
}

} // namespace

auto JsonValue::begin() -> JsonValueIter
{
    return {.at = this + 1};
}

auto JsonValue::end() -> JsonValueIter
{
    return {.at = col.end};
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

//

auto JsonValueIter::operator==(const JsonValueIter &rhs) const -> bool
{
    return at == rhs.at;
}

auto JsonValueIter::operator++() -> JsonValueIter &
{
    switch (at->tag)
    {
    case JsonValue::Tag::ArrayBegin:
    case JsonValue::Tag::ObjectBegin: {
        at = at->col.end + 1;
        break;
    }

    default: {
        ++at;
        break;
    }
    }

    return *this;
}

auto JsonValueIter::operator+=(uint32_t i) -> JsonValueIter &
{
    while (i-- > 0)
        ++(*this);
    return *this;
}

auto JsonValueIter::operator*() -> JsonValue *
{
    return at;
}

//

void LogJsonValue(JsonValue *val, uint32_t indent)
{
    using Tag = JsonValue::Tag;

    switch (val->tag)
    {
    case Tag::Null: {
        NYLA_LOG("%*snull", indent, " ");
        return;
    }
    case Tag::Bool: {
        NYLA_LOG("%*s%d", indent, " ", val->b);
        return;
    }
    case Tag::Integer: {
        NYLA_LOG("%*s%" PRIu64, indent, " ", val->i);
        return;
    }
    case Tag::Float: {
        NYLA_LOG("%*s%f", indent, " ", val->f);
        return;
    }
    case Tag::String: {
        NYLA_LOG("%*s\"" NYLA_SV_FMT "\"", indent, " ", NYLA_SV_ARG(val->s));
        return;
    }
    case Tag::ArrayBegin: {
        NYLA_LOG("%*s[", indent, " ");

        auto end = val->end();
        for (auto it = val->begin(); it != end; ++it)
            LogJsonValue(*it, indent + 2);

        // Fallthrough
    }
    case Tag::ArrayEnd: {
        NYLA_LOG("%*s]", indent, " ");
        return;
    }
    case Tag::ObjectBegin: {
        NYLA_LOG("%*s{", indent, " ");

        auto end = val->end();
        for (auto it = val->begin(); it != end; ++it)
        {
            LogJsonValue(*it, indent + 2);
        }

        // Fallthrough
    }
    case Tag::ObjectEnd: {
        NYLA_LOG("%*s}", indent, " ");
        return;
    }
    default: {
        NYLA_ASSERT(false);
    }
    }
}

} // namespace nyla