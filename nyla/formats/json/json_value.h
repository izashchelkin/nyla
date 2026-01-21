#pragma once

#include "nyla/commons/assert.h"
#include <cstdint>
#include <span>
#include <string_view>

namespace nyla
{

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

    auto TryVisitAny(std::span<std::string_view> path, JsonValue *&out) -> bool;
    auto VisitAny(std::span<std::string_view> path) -> JsonValue *
    {
        JsonValue *out;
        NYLA_ASSERT(TryVisitAny(path, out));
        return out;
    }

    auto TryVisitObject(std::span<std::string_view> path, JsonValue *&out) -> bool;
    auto VisitObject(std::span<std::string_view> path) -> JsonValue *
    {
        JsonValue *out;
        NYLA_ASSERT(TryVisitObject(path, out));
        return out;
    }

    auto TryVisitArray(std::span<std::string_view> path, JsonValue *&out) -> bool;
    auto VisitArray(std::span<std::string_view> path) -> JsonValue *
    {
        JsonValue *out;
        NYLA_ASSERT(TryVisitArray(path, out));
        return out;
    }

    auto TryVisitString(std::span<std::string_view> path, std::string_view &out) -> bool;
    auto VisitString(std::span<std::string_view> path) -> std::string_view
    {
        std::string_view out;
        NYLA_ASSERT(TryVisitString(path, out));
        return out;
    }

    auto TryVisitInteger(std::span<std::string_view> path, uint64_t &out) -> bool;
    auto VisitInteger(std::span<std::string_view> path) -> uint64_t
    {
        uint64_t out;
        NYLA_ASSERT(TryVisitInteger(path, out));
        return out;
    }
};

} // namespace nyla