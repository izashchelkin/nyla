#include "nyla/commons/json_value.h"

#include <cinttypes>
#include <cstdint>

#include "nyla/commons/fmt.h"

namespace nyla
{

namespace JsonValue
{

auto TryAny(json_value &self, span<byteview> path, json_value *&out) -> bool
{
    if (Span::IsEmpty(path))
    {
        out = &self;
        return true;
    }

    if (self.tag != json_tag::ObjectBegin)
        return false;

    for (auto it = self.begin(), end = self.end(); it != end;)
    {
        byteview key = JsonValue::String(*it);
        ++it;
        auto val = JsonValue::Any(*it);
        ++it;

        if (Span::Eq(key, Span::Front(path)))
            return JsonValue::TryAny(*val, Span::SubSpan(path, 1), out);
    }

    return false;
}

auto TryObject(json_value &self, span<byteview> path, json_value *&out) -> bool
{
    json_value *tmp;
    if (!TryAny(self, path, tmp))
        return false;
    if (tmp->tag != json_tag::ObjectBegin)
        return false;

    out = tmp;
    return true;
}

auto TryArray(json_value &self, span<byteview> path, json_value *&out) -> bool
{
    json_value *tmp;
    if (!TryAny(self, path, tmp))
        return false;
    if (tmp->tag != json_tag::ArrayBegin)
        return false;

    out = tmp;
    return true;
}

auto TryString(json_value &self, span<byteview> path, byteview &out) -> bool
{
    json_value *tmp;
    if (!TryAny(self, path, tmp))
        return false;
    if (tmp->tag != json_tag::String)
        return false;

    out = tmp->val.valStr;
    return true;
}

auto TryQWord(json_value &self, span<byteview> path, uint64_t &out) -> bool
{
    json_value *tmp;
    if (!TryAny(self, path, tmp))
        return false;
    if (tmp->tag != json_tag::Integer)
        return false;

    out = tmp->val.valInt;
    return true;
}

auto TryDWord(json_value &self, span<byteview> path, uint32_t &out) -> bool
{
    json_value *tmp;
    if (!TryAny(self, path, tmp))
        return false;
    if (tmp->tag != json_tag::Integer)
        return false;

    out = tmp->val.valInt;
    return true;
}

auto TryDouble(json_value &self, span<byteview> path, double &out) -> bool
{
    json_value *tmp;
    if (!TryAny(self, path, tmp))
        return false;
    if (tmp->tag != json_tag::Double)
        return false;

    out = tmp->val.valDouble;
    return true;
}

auto TryBool(json_value &self, span<byteview> path, bool &out) -> bool
{
    json_value *tmp;
    if (!TryAny(self, path, tmp))
        return false;
    if (tmp->tag != json_tag::Bool)
        return false;

    out = tmp->val.valBool;
    return true;
}

auto GetNext(json_value &self) -> json_value *
{
    switch (self.tag)
    {
    case json_tag::ArrayBegin:
    case json_tag::ObjectBegin:
        return self.val.valHeader.end + 1;

    default:
        return &self + 1;
    }
}

void Log(json_value &self, uint32_t indent)
{
    switch (self.tag)
    {
    case json_tag::Null: {
        NYLA_LOG("%*snull", indent, " ");
        return;
    }
    case json_tag::Bool: {
        NYLA_LOG("%*s%d", indent, " ", JsonValue::Bool(self));
        return;
    }
    case json_tag::Integer: {
        NYLA_LOG("%*s%" PRIu64, indent, " ", JsonValue::QWord(self));
        return;
    }
    case json_tag::Double: {
        NYLA_LOG("%*s%f", indent, " ", JsonValue::Double(self));
        return;
    }
    case json_tag::String: {
        NYLA_LOG("%*s\"" NYLA_SV_FMT "\"", indent, " ", NYLA_SV_ARG(JsonValue::String(self)));
        return;
    }
    case json_tag::ArrayBegin: {
        NYLA_LOG("%*s[", indent, " ");

        auto end = self.end();
        for (auto it = self.begin(); it != end; ++it)
            Log(*it, indent + 2);

        // Fallthrough
    }
    case json_tag::ArrayEnd: {
        NYLA_LOG("%*s]", indent, " ");
        return;
    }
    case json_tag::ObjectBegin: {
        NYLA_LOG("%*s{", indent, " ");

        auto end = self.end();
        for (auto it = self.begin(); it != end; ++it)
        {
            Log(*it, indent + 2);
        }

        // Fallthrough
    }
    case json_tag::ObjectEnd: {
        NYLA_LOG("%*s}", indent, " ");
        return;
    }
    default: {
        NYLA_ASSERT(false);
    }
    }
}

} // namespace JsonValue

} // namespace nyla