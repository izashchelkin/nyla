#pragma once

#include "nyla/commons/assert.h"
#include <cstdint>
#include <span>
#include <string_view>

namespace nyla
{

struct JsonValue;

class JsonValueIter
{
  public:
    explicit JsonValueIter(JsonValue *at) : m_At{at}
    {
    }

    auto operator*() const -> JsonValue *
    {
        return m_At;
    }
    auto operator->() const -> JsonValue *
    {
        return m_At;
    }
    auto operator==(const JsonValueIter &other) const -> bool
    {
        return m_At == other.m_At;
    }

    auto operator++() -> JsonValueIter &;

  private:
    JsonValue *m_At;
};

enum class JsonTag
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

class JsonValue
{
  public:
    JsonValue() : m_Tag{JsonTag::Invalid}, m_Val{}
    {
    }

    explicit JsonValue(JsonTag tag) : m_Tag{tag}, m_Val{}
    {
    }

    explicit JsonValue(JsonTag tag, uint32_t count, JsonValue *end)
        : m_Tag{tag}, m_Val{.valHeader = {.end = end, .count = count}}
    {
    }

    explicit JsonValue(bool val) : m_Tag{JsonTag::Bool}, m_Val{.valBool = val}
    {
    }

    explicit JsonValue(uint64_t val) : m_Tag{JsonTag::Integer}, m_Val{.valInt = val}
    {
    }

    explicit JsonValue(double val) : m_Tag{JsonTag::Double}, m_Val{.valDouble = val}
    {
    }

    explicit JsonValue(std::string_view val) : m_Tag{JsonTag::String}, m_Val{.valSv = val}
    {
    }

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
    }                                                                                                                  \
    auto Try##name(out &ret) -> bool                                                                                   \
    {                                                                                                                  \
        return Try##name(std::span<std::string_view>{}, ret);                                                          \
    }                                                                                                                  \
    auto name() -> out                                                                                                 \
    {                                                                                                                  \
        return name(std::span<std::string_view>{});                                                                    \
    }

    DECL(Any, JsonValue *);
    DECL(Object, JsonValue *);
    DECL(Array, JsonValue *);
    DECL(String, std::string_view);
    DECL(DWord, uint32_t);
    DECL(QWord, uint64_t);
    DECL(Double, double);
    DECL(Bool, bool);

#undef DECL

    auto Skip() -> JsonValue *;

    //

    auto GetTag()
    {
        return m_Tag;
    }

    auto GetCount()
    {
        NYLA_ASSERT(m_Tag == JsonTag::ArrayBegin || m_Tag == JsonTag::ObjectBegin);
        return m_Val.valHeader.count;
    }

    auto GetFront() -> JsonValue *
    {
        NYLA_ASSERT(m_Tag == JsonTag::ArrayBegin || m_Tag == JsonTag::ObjectBegin);
        return this + 1;
    }

#if 0
    auto operator[](uint32_t i) -> JsonValue *
    {
        NYLA_ASSERT(m_Tag == JsonTag::ArrayBegin || m_Tag == JsonTag::ObjectBegin);

        JsonValue *ret = this;
        while (i-- > 0)
            ret = ret->Skip();

        return ret;
    }
#endif

    auto begin() -> JsonValueIter
    {
        NYLA_ASSERT(m_Tag == JsonTag::ArrayBegin || m_Tag == JsonTag::ObjectBegin);
        return JsonValueIter{this + 1};
    }

    auto end() -> JsonValueIter
    {
        NYLA_ASSERT(m_Tag == JsonTag::ArrayBegin || m_Tag == JsonTag::ObjectBegin);
        return JsonValueIter{m_Val.valHeader.end};
    }

  private:
    JsonTag m_Tag;
    union {
        bool valBool;
        uint64_t valInt;
        double valDouble;
        std::string_view valSv;

        struct
        {
            JsonValue *end;
            uint32_t count;
        } valHeader;
    } m_Val;
};

void LogJsonValue(JsonValue *val, uint32_t indent = 0);

} // namespace nyla