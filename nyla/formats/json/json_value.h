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
    JsonValue *end;
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

} // namespace nyla