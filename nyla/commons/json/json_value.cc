#include "nyla/commons/json/json_value.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/log.h"
#include "nyla/commons/json/json.h"
#include <cinttypes>
#include <cstdint>
#include <string_view>

namespace nyla
{

auto JsonValue::Skip() -> JsonValue *
{
    switch (m_Tag)
    {
    case JsonTag::ArrayBegin:
    case JsonTag::ObjectBegin:
        return m_Val.valHeader.end + 1;

    default:
        return this + 1;
    }
}

auto JsonValue::TryAny(Span<Str> path, JsonValue *&out) -> bool
{
    if (path.empty())
    {
        out = this;
        return true;
    }

    if (m_Tag != JsonTag::ObjectBegin)
        return false;

    auto end = this->end();
    for (auto it = begin(); it != end;)
    {
        auto key = it->String();
        ++it;
        auto val = it->Any();
        ++it;

        if (key == path.front())
            return val->TryAny(path.subspan(1), out);
    }

    return false;
}

auto JsonValue::TryObject(Span<Str> path, JsonValue *&out) -> bool
{
    JsonValue *tmp;
    if (!TryAny(path, tmp))
        return false;
    if (tmp->m_Tag != JsonTag::ObjectBegin)
        return false;

    out = tmp;
    return true;
}

auto JsonValue::TryArray(Span<Str> path, JsonValue *&out) -> bool
{
    JsonValue *tmp;
    if (!TryAny(path, tmp))
        return false;
    if (tmp->m_Tag != JsonTag::ArrayBegin)
        return false;

    out = tmp;
    return true;
}

auto JsonValue::TryString(Span<Str> path, Str &out) -> bool
{
    JsonValue *tmp;
    if (!TryAny(path, tmp))
        return false;
    if (tmp->m_Tag != JsonTag::String)
        return false;

    const auto &valStr = tmp->m_Val.valStr;
    out = Str{valStr.str, valStr.len};
    return true;
}

auto JsonValue::TryQWord(Span<Str> path, uint64_t &out) -> bool
{
    JsonValue *tmp;
    if (!TryAny(path, tmp))
        return false;
    if (tmp->m_Tag != JsonTag::Integer)
        return false;

    out = tmp->m_Val.valInt;
    return true;
}

auto JsonValue::TryDWord(Span<Str> path, uint32_t &out) -> bool
{
    JsonValue *tmp;
    if (!TryAny(path, tmp))
        return false;
    if (tmp->m_Tag != JsonTag::Integer)
        return false;

    out = tmp->m_Val.valInt;
    return true;
}

auto JsonValue::TryDouble(Span<Str> path, double &out) -> bool
{
    JsonValue *tmp;
    if (!TryAny(path, tmp))
        return false;
    if (tmp->m_Tag != JsonTag::Double)
        return false;

    out = tmp->m_Val.valDouble;
    return true;
}

auto JsonValue::TryBool(Span<Str> path, bool &out) -> bool
{
    JsonValue *tmp;
    if (!TryAny(path, tmp))
        return false;
    if (tmp->m_Tag != JsonTag::Bool)
        return false;

    out = tmp->m_Val.valBool;
    return true;
}

//

auto JsonValueIter::operator++() -> JsonValueIter &
{
    m_At = m_At->Skip();
    return *this;
}

//

void LogJsonValue(JsonValue *val, uint32_t indent)
{
    switch (val->GetTag())
    {
    case JsonTag::Null: {
        NYLA_LOG("%*snull", indent, " ");
        return;
    }
    case JsonTag::Bool: {
        NYLA_LOG("%*s%d", indent, " ", val->Bool());
        return;
    }
    case JsonTag::Integer: {
        NYLA_LOG("%*s%" PRIu64, indent, " ", val->QWord());
        return;
    }
    case JsonTag::Double: {
        NYLA_LOG("%*s%f", indent, " ", val->Double());
        return;
    }
    case JsonTag::String: {
        NYLA_LOG("%*s\"" NYLA_SV_FMT "\"", indent, " ", NYLA_SV_ARG(val->String()));
        return;
    }
    case JsonTag::ArrayBegin: {
        NYLA_LOG("%*s[", indent, " ");

        auto end = val->end();
        for (auto it = val->begin(); it != end; ++it)
            LogJsonValue(*it, indent + 2);

        // Fallthrough
    }
    case JsonTag::ArrayEnd: {
        NYLA_LOG("%*s]", indent, " ");
        return;
    }
    case JsonTag::ObjectBegin: {
        NYLA_LOG("%*s{", indent, " ");

        auto end = val->end();
        for (auto it = val->begin(); it != end; ++it)
        {
            LogJsonValue(*it, indent + 2);
        }

        // Fallthrough
    }
    case JsonTag::ObjectEnd: {
        NYLA_LOG("%*s}", indent, " ");
        return;
    }
    default: {
        NYLA_ASSERT(false);
    }
    }
}

} // namespace nyla