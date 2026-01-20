#include "nyla/commons/log.h"
#include "nyla/commons/memory/region_alloc.h"
#include "nyla/formats/json/json_parser.h"
#include "nyla/platform/platform.h"

namespace nyla
{

void LogJsonValue(JsonParser::Value *val)
{
    using Tag = JsonParser::Value::Tag;

    switch (val->tag)
    {
    case Tag::Null: {
        NYLA_LOG("null");
        return;
    }
    case Tag::Bool: {
        NYLA_LOG("Bool: %b", val->b);
        return;
    }
    case Tag::Integer: {
        NYLA_LOG("Integer: %lu", val->i);
        return;
    }
    case Tag::Float: {
        NYLA_LOG("Float: %f", val->f);
        return;
    }
    case Tag::String: {
        NYLA_LOG("String: " NYLA_SV_FMT, NYLA_SV_ARG(val->s));
        return;
    }
    case Tag::ArrayBegin: {
        NYLA_LOG("Array of %d elems", val->len);

        for (uint32_t i = 0; i < val->len; ++i)
            LogJsonValue(val + 1 + i);

        return;
    }
    case Tag::ObjectBegin: {
        NYLA_LOG("Object of %d elems", val->len);

        for (uint32_t i = 0; i < val->len * 2; ++i)
            LogJsonValue(val + 1 + i);

        return;
    }
    default: {
        NYLA_ASSERT(false);
    }
    }
}

auto PlatformMain() -> int
{
    g_Platform->Init({});

    RegionAlloc regionAlloc{g_Platform->ReserveMemPages(1_MiB), 0, 1_MiB, RegionAllocCommitPageGrowth::GetInstance()};

    {
        const char json[] = "52";
        JsonParser parser{regionAlloc, json, sizeof(json)};
        JsonParser::Value *value = parser.ParseNext();

        LogJsonValue(value);
    }
    regionAlloc.Reset();
    NYLA_LOG();

    {
        const char json[] = "52.02";
        JsonParser parser{regionAlloc, json, sizeof(json)};
        JsonParser::Value *value = parser.ParseNext();

        LogJsonValue(value);
    }
    regionAlloc.Reset();

    NYLA_LOG();

    {
        const char json[] = "[52, 23]";
        JsonParser parser{regionAlloc, json, sizeof(json)};
        JsonParser::Value *value = parser.ParseNext();

        LogJsonValue(value);
    }
    regionAlloc.Reset();

    NYLA_LOG();

    {
        const char json[] = "[\"Hello world\", 23]";
        JsonParser parser{regionAlloc, json, sizeof(json)};
        JsonParser::Value *value = parser.ParseNext();

        LogJsonValue(value);
    }
    regionAlloc.Reset();

    NYLA_LOG();

    {
        const char json[] = "{\"Hello\": false}";
        JsonParser parser{regionAlloc, json, sizeof(json)};
        JsonParser::Value *value = parser.ParseNext();

        LogJsonValue(value);
    }
    regionAlloc.Reset();

    NYLA_LOG();

    return 0;
}

} // namespace nyla