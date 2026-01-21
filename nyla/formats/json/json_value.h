#pragma once

#include "nyla/commons/assert.h"
#include <cstdint>
#include <span>
#include <string_view>

namespace nyla
{

struct JsonValue;

struct JsonValueIter
{
    JsonValue *at;

    auto operator++() -> JsonValueIter &;
    auto operator+=(uint32_t i) -> JsonValueIter &;
    auto operator*() -> JsonValue *;
    auto operator==(const JsonValueIter &other) const -> bool;
};

struct JsonValue
{
    enum class Tag
    {
        Null,
        Bool,
        Integer,
        Float,
        String,
        ArrayBegin,
        ArrayEnd,
        ObjectBegin,
        ObjectEnd,
    };

    Tag tag;
    union {
        bool b;
        uint64_t i;
        double f;
        std::string_view s;

        struct
        {
            JsonValue *end;
            uint32_t len;
        } col;
    };

#define DECL(name, out)                                                                                                \
    auto Try##name(std::span<std::string_view>, out &) -> bool;                                                        \
    auto name(std::span<std::string_view> path) -> out                                                                 \
    {                                                                                                                  \
        out ret;                                                                                                       \
        NYLA_ASSERT(Try##name(path, ret));                                                                             \
        return ret;                                                                                                    \
    }                                                                                                                  \
    auto Try##name(std::string_view path, out &ret) -> bool                                                            \
    {                                                                                                                  \
        return Try##name(std::span{&path, 1}, ret);                                                                    \
    }                                                                                                                  \
    auto name(std::string_view path) -> out                                                                            \
    {                                                                                                                  \
        return name(std::span{&path, 1});                                                                              \
    }

    DECL(Any, JsonValue *);
    DECL(Object, JsonValue *);
    DECL(Array, JsonValue *);
    DECL(String, std::string_view);
    DECL(Integer, uint64_t);

#undef DECL

    auto begin() -> JsonValueIter;
    auto end() -> JsonValueIter;
};

inline auto JsonValueIter::operator==(const JsonValueIter &rhs) const -> bool
{
    return at == rhs.at;
}

inline auto JsonValueIter::operator++() -> JsonValueIter &
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

inline auto JsonValueIter::operator+=(uint32_t i) -> JsonValueIter &
{
    while (i-- > 0)
        ++(*this);
    return *this;
}

inline auto JsonValueIter::operator*() -> JsonValue *
{
    return at;
}

} // namespace nyla