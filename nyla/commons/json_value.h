#pragma once

#include <cstdint>

#include "nyla/commons/fmt.h"
#include "nyla/commons/span.h"

namespace nyla
{

struct json_value;

namespace JsonValue
{

auto GetNext(json_value &self) -> json_value *;

void Log(json_value *val, uint32_t indent = 0);

} // namespace JsonValue

struct json_value_iterator
{
    json_value *p;

    auto operator*() const -> json_value &
    {
        return *p;
    }

    auto operator->() const -> json_value &
    {
        return *p;
    }

    auto operator==(const json_value_iterator &other) const -> bool
    {
        return p == other.p;
    }

    auto operator++() -> json_value_iterator &
    {
        p = JsonValue::GetNext(*p);
        return *this;
    }
};

enum class json_tag
{
    Invalid = 0,
    Null,
    Bool,
    Integer,
    Double,
    String,
    ArrayBegin,
    ArrayEnd,
    ObjectBegin,
    ObjectEnd,
};

struct json_value
{
    json_tag tag;
    union {
        bool valBool;
        int64_t valInt;
        double valDouble;
        byteview valStr;

        struct
        {
            json_value *end;
            uint32_t count;
        } valHeader;
    } val;

    auto begin() -> json_value_iterator
    {
        NYLA_ASSERT(tag == json_tag::ArrayBegin || tag == json_tag::ObjectBegin);
        return json_value_iterator{this + 1};
    }

    auto end() -> json_value_iterator
    {
        NYLA_ASSERT(tag == json_tag::ArrayBegin || tag == json_tag::ObjectBegin);
        return json_value_iterator{val.valHeader.end};
    }
};

namespace JsonValue
{

INLINE auto GetCount(const json_value &self)
{
    NYLA_ASSERT(self.tag == json_tag::ArrayBegin || self.tag == json_tag::ObjectBegin);
    return self.val.valHeader.count;
}

INLINE auto GetFront(json_value &self) -> json_value *
{
    NYLA_ASSERT(self.tag == json_tag::ArrayBegin || self.tag == json_tag::ObjectBegin);
    return &self + 1;
}

INLINE void SetValue(json_value &self, json_tag tag)
{
    self.tag = tag;
}

INLINE void SetValue(json_value &self, bool val)
{
    self.tag = json_tag::Bool;
    self.val = {.valBool = val};
}

INLINE void SetValue(json_value &self, int64_t val)
{
    self.tag = json_tag::Integer;
    self.val = {.valInt = val};
}

INLINE void SetValue(json_value &self, double val)
{
    self.tag = json_tag::Double;
    self.val = {.valDouble = val};
}

INLINE void SetValue(json_value &self, byteview str)
{
    self.tag = json_tag::String;
    self.val = {.valStr = str};
}

INLINE void SetValue(json_value &self, json_tag tag, uint32_t count, json_value *end)
{
    self.tag = tag;
    self.val.valHeader = {
        .end = end,
        .count = count,
    };
}

#define X(name, out)                                                                                                   \
    auto Try##name(json_value &self, span<byteview>, out &) -> bool;                                                   \
    INLINE auto name(json_value &self, span<byteview> path) -> out                                                     \
    {                                                                                                                  \
        out ret;                                                                                                       \
        NYLA_ASSERT(Try##name(self, path, ret));                                                                       \
        return ret;                                                                                                    \
    }                                                                                                                  \
    INLINE auto Try##name(json_value &self, byteview path, out &ret) -> bool                                           \
    {                                                                                                                  \
        return Try##name(self, span{&path, 1}, ret);                                                                   \
    }                                                                                                                  \
    INLINE auto name(json_value &self, byteview path) -> out                                                           \
    {                                                                                                                  \
        return name(self, span{&path, 1});                                                                             \
    }                                                                                                                  \
    INLINE auto Try##name(json_value &self, out &ret) -> bool                                                          \
    {                                                                                                                  \
        return Try##name(self, span<byteview>{}, ret);                                                                 \
    }                                                                                                                  \
    INLINE auto name(json_value &self) -> out                                                                          \
    {                                                                                                                  \
        return name(self, span<byteview>{});                                                                           \
    }

X(Any, json_value *);
X(Object, json_value *);
X(Array, json_value *);
X(String, byteview);
X(DWord, uint32_t);
X(QWord, uint64_t);
X(Double, double);
X(Bool, bool);

#undef X

} // namespace JsonValue

} // namespace nyla