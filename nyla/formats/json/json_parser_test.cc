#include "nyla/commons/log.h"
#include "nyla/commons/memory/region_alloc.h"
#include "nyla/formats/json/json_parser.h"
#include "nyla/formats/json/json_value.h"
#include "nyla/platform/platform.h"
#include <cinttypes>
#include <cstdint>
#include <string_view>

namespace nyla
{

auto PlatformMain() -> int
{
    g_Platform->Init({});

    RegionAlloc regionAlloc{g_Platform->ReserveMemPages(1_MiB), 0, 1_MiB, RegionAllocCommitPageGrowth::GetInstance()};

    goto test6;

test0: {
    const char json[] = "52";
    JsonParser parser{regionAlloc, json, sizeof(json)};
    JsonValue *value = parser.ParseNext();

    LogJsonValue(value);
}
    regionAlloc.Reset();
    NYLA_LOG();

test1: {
    const char json[] = "52.02";
    JsonParser parser{regionAlloc, json, sizeof(json)};
    JsonValue *value = parser.ParseNext();

    LogJsonValue(value);
}
    regionAlloc.Reset();

    NYLA_LOG();

test2: {
    const char json[] = "[52, 23]";
    JsonParser parser{regionAlloc, json, sizeof(json)};
    JsonValue *value = parser.ParseNext();

    LogJsonValue(value);
}
    regionAlloc.Reset();

    NYLA_LOG();

test3: {
    const char json[] = "[\"Hello world\", 23]";
    JsonParser parser{regionAlloc, json, sizeof(json)};
    JsonValue *value = parser.ParseNext();

    LogJsonValue(value);
}
    regionAlloc.Reset();

    NYLA_LOG();

test4: {
    const char json[] = "{\"Hello\": false}";
    JsonParser parser{regionAlloc, json, sizeof(json)};
    JsonValue *value = parser.ParseNext();

    LogJsonValue(value);
}
    regionAlloc.Reset();

    NYLA_LOG();

test5: {
    const char json[] =
        R"({ "meshes": [ { "name": "Cube", "primitives": [ { "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 }, "indices": 3, "material": 0 } ] } ] })";
    JsonParser parser{regionAlloc, json, sizeof(json)};
    JsonValue *value = parser.ParseNext();

    LogJsonValue(value);
}
    regionAlloc.Reset();

    NYLA_LOG();

test6: {
    const char json[] =
        R"({ "asset": { "generator": "Khronos glTF Blender I/O v5.0.21", "version": "2.0" }, "scene": 0, "scenes": [ { "name": "Scene", "nodes": [ 0 ] } ], "nodes": [ { "mesh": 0, "name": "Cube" } ], "materials": [ { "doubleSided": true, "name": "Material", "pbrMetallicRoughness": { "baseColorFactor": [ 0.800000011920929, 0.800000011920929, 0.800000011920929, 1 ], "metallicFactor": 0, "roughnessFactor": 0.5 } } ], "meshes": [ { "name": "Cube", "primitives": [ { "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 }, "indices": 3, "material": 0 } ] } ], "accessors": [ { "bufferView": 0, "componentType": 5126, "count": 24, "max": [ 1, 1, 1 ], "min": [ -1, -1, -1 ], "type": "VEC3" }, { "bufferView": 1, "componentType": 5126, "count": 24, "type": "VEC3" }, { "bufferView": 2, "componentType": 5126, "count": 24, "type": "VEC2" }, { "bufferView": 3, "componentType": 5123, "count": 36, "type": "SCALAR" } ], "bufferViews": [ { "buffer": 0, "byteLength": 288, "byteOffset": 0, "target": 34962 }, { "buffer": 0, "byteLength": 288, "byteOffset": 288, "target": 34962 }, { "buffer": 0, "byteLength": 192, "byteOffset": 576, "target": 34962 }, { "buffer": 0, "byteLength": 72, "byteOffset": 768, "target": 34963 } ], "buffers": [ { "byteLength": 840 } ] })";
    JsonParser parser{regionAlloc, json, sizeof(json)};
    JsonValue *value = parser.ParseNext();

    JsonValue *asset = value->Object("asset");

    {
        std::string_view sv = asset->String("generator");
        NYLA_LOG("" NYLA_SV_FMT, NYLA_SV_ARG(sv));
    }

    {
        std::string_view sv = asset->String("version");
        NYLA_LOG("" NYLA_SV_FMT, NYLA_SV_ARG(sv));
    }

    LogJsonValue(value);

    JsonValue *buffers = value->Array("buffers");
}
    regionAlloc.Reset();

    NYLA_LOG();

    return 0;
}

} // namespace nyla