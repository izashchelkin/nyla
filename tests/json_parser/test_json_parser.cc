#include "nyla/commons/entrypoint.h"
#include "nyla/commons/json_parser.h"
#include "nyla/commons/json_value.h"
#include "nyla/commons/region_alloc.h"

#include <cstdio>
#include <cstring>

namespace nyla
{
namespace
{

auto Pass(int &p, int &f, const char *name) -> void
{
    printf("  PASS %s\n", name);
    p++;
}
auto Fail(int &p, int &f, const char *name, const char *msg = nullptr) -> void
{
    printf("  FAIL %s", name);
    if (msg)
        printf(": %s", msg);
    printf("\n");
    f++;
}

auto ParseJson(byteview input, region_alloc &alloc) -> json_value *
{
    auto storage = RegionAlloc::AllocArray<json_value>(alloc, 2048);
    json_parser parser;
    JsonParser::Init(parser, input, storage);
    return JsonParser::ParseNext(parser);
}

// ─── Basic types ──────────────────────────────────────────────────────────────

void TestBasicTypes(region_alloc &alloc, int &passed, int &failed)
{
    printf("─── Basic types ───\n");

    // null
    {
        auto *root = ParseJson("null"_s, alloc);
        if (root && root->tag == json_tag::Null)
            Pass(passed, failed, "null");
        else
            Fail(passed, failed, "null");
    }

    // bool true
    {
        auto *root = ParseJson("true"_s, alloc);
        if (root && root->tag == json_tag::Bool && root->val.valBool)
            Pass(passed, failed, "bool true");
        else
            Fail(passed, failed, "bool true");
    }

    // bool false
    {
        auto *root = ParseJson("false"_s, alloc);
        if (root && root->tag == json_tag::Bool && !root->val.valBool)
            Pass(passed, failed, "bool false");
        else
            Fail(passed, failed, "bool false");
    }

    // integer
    {
        auto *root = ParseJson("42"_s, alloc);
        if (root && root->tag == json_tag::Integer && root->val.valInt == 42)
            Pass(passed, failed, "integer 42");
        else
            Fail(passed, failed, "integer 42");
    }

    // negative integer
    {
        auto *root = ParseJson("-123"_s, alloc);
        if (root && root->tag == json_tag::Integer && root->val.valInt == -123)
            Pass(passed, failed, "integer -123");
        else
            Fail(passed, failed, "integer -123");
    }

    // double
    {
        auto *root = ParseJson("3.14"_s, alloc);
        if (root && root->tag == json_tag::Double && root->val.valDouble > 3.13 && root->val.valDouble < 3.15)
            Pass(passed, failed, "double 3.14");
        else
            Fail(passed, failed, "double 3.14");
    }

    // string (simple)
    {
        auto *root = ParseJson("\"hello\""_s, alloc);
        bool ok = root && root->tag == json_tag::String &&
                  root->val.valStr.size == 5 &&
                  MemEq(root->val.valStr.data, "hello", 5);
        if (ok)
            Pass(passed, failed, "string 'hello'");
        else
            Fail(passed, failed, "string 'hello'");
    }

    // empty string
    {
        auto *root = ParseJson("\"\""_s, alloc);
        if (root && root->tag == json_tag::String && root->val.valStr.size == 0)
            Pass(passed, failed, "empty string");
        else
            Fail(passed, failed, "empty string");
    }
}

// ─── Arrays ───────────────────────────────────────────────────────────────────

void TestArrays(region_alloc &alloc, int &passed, int &failed)
{
    printf("─── Arrays ───\n");

    // empty array
    {
        auto *root = ParseJson("[]"_s, alloc);
        if (root && root->tag == json_tag::ArrayBegin && JsonValue::GetCount(*root) == 0)
            Pass(passed, failed, "empty array");
        else
            Fail(passed, failed, "empty array");
    }

    // single-element array
    {
        auto *root = ParseJson("[1]"_s, alloc);
        bool ok = root && root->tag == json_tag::ArrayBegin && JsonValue::GetCount(*root) == 1;
        if (ok)
        {
            auto *elem = JsonValue::GetFront(*root);
            ok = elem && elem->tag == json_tag::Integer && elem->val.valInt == 1;
        }
        if (ok)
            Pass(passed, failed, "[1]");
        else
            Fail(passed, failed, "[1]");
    }

    // multi-element array
    {
        auto *root = ParseJson("[1, 2, 3]"_s, alloc);
        bool ok = root && root->tag == json_tag::ArrayBegin && JsonValue::GetCount(*root) == 3;
        if (ok)
        {
            int sum = 0;
            for (auto it = root->begin(), end = root->end(); it != end; ++it)
                sum += (int)(*it).val.valInt;
            ok = (sum == 6);
        }
        if (ok)
            Pass(passed, failed, "[1,2,3] sum=6");
        else
            Fail(passed, failed, "[1,2,3] sum=6");
    }

    // string array
    {
        auto *root = ParseJson("[\"a\", \"b\"]"_s, alloc);
        bool ok = root && root->tag == json_tag::ArrayBegin && JsonValue::GetCount(*root) == 2;
        if (ok)
        {
            auto it = root->begin();
            ok = (*it).tag == json_tag::String && (*it).val.valStr.size == 1 && (*it).val.valStr.data[0] == 'a';
            ++it;
            ok = ok && (*it).tag == json_tag::String && (*it).val.valStr.size == 1 && (*it).val.valStr.data[0] == 'b';
        }
        if (ok)
            Pass(passed, failed, "[\"a\",\"b\"]");
        else
            Fail(passed, failed, "[\"a\",\"b\"]");
    }

    // nested array
    {
        auto *root = ParseJson("[[1,2],[3]]"_s, alloc);
        bool ok = root && root->tag == json_tag::ArrayBegin && JsonValue::GetCount(*root) == 2;
        if (ok)
        {
            auto it = root->begin();
            ok = (*it).tag == json_tag::ArrayBegin && JsonValue::GetCount(*it) == 2;
            ++it;
            ok = ok && (*it).tag == json_tag::ArrayBegin && JsonValue::GetCount(*it) == 1;
        }
        if (ok)
            Pass(passed, failed, "nested [[1,2],[3]]");
        else
            Fail(passed, failed, "nested [[1,2],[3]]");
    }
}

// ─── Objects ──────────────────────────────────────────────────────────────────

void TestObjects(region_alloc &alloc, int &passed, int &failed)
{
    printf("─── Objects ───\n");

    // empty object
    {
        auto *root = ParseJson("{}"_s, alloc);
        if (root && root->tag == json_tag::ObjectBegin && JsonValue::GetCount(*root) == 0)
            Pass(passed, failed, "empty object");
        else
            Fail(passed, failed, "empty object");
    }

    // single key
    {
        auto *root = ParseJson("{\"x\": 1}"_s, alloc);
        bool ok = root && root->tag == json_tag::ObjectBegin && JsonValue::GetCount(*root) == 1;
        if (ok)
        {
            auto it = root->begin();
            ok = (*it).tag == json_tag::String && (*it).val.valStr.size == 1 && (*it).val.valStr.data[0] == 'x';
            ++it;
            ok = ok && (*it).tag == json_tag::Integer && (*it).val.valInt == 1;
        }
        if (ok)
            Pass(passed, failed, "{\"x\":1}");
        else
            Fail(passed, failed, "{\"x\":1}");
    }

    // multi-key object
    {
        auto *root = ParseJson("{\"a\": 1, \"b\": 2}"_s, alloc);
        bool ok = root && root->tag == json_tag::ObjectBegin && JsonValue::GetCount(*root) == 2;
        if (ok)
        {
            auto it = root->begin();
            ok = (*it).tag == json_tag::String && (*it).val.valStr.data[0] == 'a';
            ++it;
            ok = ok && (*it).tag == json_tag::Integer && (*it).val.valInt == 1;
            ++it;
            ok = ok && (*it).tag == json_tag::String && (*it).val.valStr.data[0] == 'b';
            ++it;
            ok = ok && (*it).tag == json_tag::Integer && (*it).val.valInt == 2;
        }
        if (ok)
            Pass(passed, failed, "{\"a\":1,\"b\":2}");
        else
            Fail(passed, failed, "{\"a\":1,\"b\":2}");
    }

    // nested object
    {
        auto *root = ParseJson("{\"a\": {\"b\": 2}}"_s, alloc);
        bool ok = root && root->tag == json_tag::ObjectBegin && JsonValue::GetCount(*root) == 1;
        if (ok)
        {
            auto it = root->begin();
            ok = (*it).tag == json_tag::String && (*it).val.valStr.size == 1 && (*it).val.valStr.data[0] == 'a';
            ++it;
            ok = ok && (*it).tag == json_tag::ObjectBegin && JsonValue::GetCount(*it) == 1;
        }
        if (ok)
            Pass(passed, failed, "nested object");
        else
            Fail(passed, failed, "nested object");
    }
}

// ─── Navigation API ───────────────────────────────────────────────────────────

void TestNavigation(region_alloc &alloc, int &passed, int &failed)
{
    printf("─── Navigation API ───\n");

    const char *json = R"({
        "name": "test",
        "count": 3,
        "items": [10, 20, 30],
        "nested": {"key": "val"}
    })";

    auto *root = ParseJson({(const uint8_t *)json, strlen(json)}, alloc);

    // JsonValue::String
    {
        auto s = JsonValue::String(*root, "name"_s);
        bool ok = (s.size == 4 && MemEq(s.data, "test", 4));
        if (ok)
            Pass(passed, failed, "JsonValue::String");
        else
            Fail(passed, failed, "JsonValue::String");
    }

    // JsonValue::DWord
    {
        auto d = JsonValue::DWord(*root, "count"_s);
        if (d == 3)
            Pass(passed, failed, "JsonValue::DWord");
        else
            Fail(passed, failed, "JsonValue::DWord");
    }

    // JsonValue::Array + iterate
    {
        auto *arr = JsonValue::Array(*root, "items"_s);
        bool ok = arr && JsonValue::GetCount(*arr) == 3;
        if (ok)
        {
            int sum = 0;
            for (auto it = arr->begin(), end = arr->end(); it != end; ++it)
                sum += (int)(*it).val.valInt;
            ok = (sum == 60);
        }
        if (ok)
            Pass(passed, failed, "JsonValue::Array + iterate");
        else
            Fail(passed, failed, "JsonValue::Array + iterate");
    }

    // JsonValue::Object nested
    {
        auto *obj = JsonValue::Object(*root, "nested"_s);
        bool ok = obj && JsonValue::GetCount(*obj) == 1;
        if (ok)
        {
            auto s = JsonValue::String(*obj, "key"_s);
            ok = (s.size == 3 && MemEq(s.data, "val", 3));
        }
        if (ok)
            Pass(passed, failed, "JsonValue::Object nested");
        else
            Fail(passed, failed, "JsonValue::Object nested");
    }

    // TryXxx variants
    {
        byteview out = {};
        bool found = JsonValue::TryString(*root, "missing"_s, out);
        if (!found && out.size == 0)
            Pass(passed, failed, "TryString missing");
        else
            Fail(passed, failed, "TryString missing");
    }

    // Bool
    {
        const char *bjson = "{\"ok\": true, \"notOk\": false}";
        auto *broot = ParseJson({(const uint8_t *)bjson, strlen(bjson)}, alloc);
        bool okv = JsonValue::Bool(*broot, "ok"_s);
        bool nok = JsonValue::Bool(*broot, "notOk"_s);
        if (okv && !nok)
            Pass(passed, failed, "JsonValue::Bool");
        else
            Fail(passed, failed, "JsonValue::Bool");
    }
}

// ─── Whitespace handling ──────────────────────────────────────────────────────

void TestWhitespace(region_alloc &alloc, int &passed, int &failed)
{
    printf("─── Whitespace ───\n");

    // lots of whitespace
    {
        auto *root = ParseJson("  {  \"x\"  :  1  ,  \"y\"  :  2  }  "_s, alloc);
        bool ok = root && root->tag == json_tag::ObjectBegin && JsonValue::GetCount(*root) == 2;
        if (ok)
            Pass(passed, failed, "whitespace-heavy object");
        else
            Fail(passed, failed, "whitespace-heavy object");
    }

    // newlines
    {
        auto *root = ParseJson("{\n\"x\":\n1\n}"_s, alloc);
        bool ok = root && JsonValue::GetCount(*root) == 1;
        if (ok)
            Pass(passed, failed, "newlines");
        else
            Fail(passed, failed, "newlines");
    }

    // tabs
    {
        auto *root = ParseJson("{\t\"x\":\t1\t}"_s, alloc);
        bool ok = root && JsonValue::GetCount(*root) == 1;
        if (ok)
            Pass(passed, failed, "tabs");
        else
            Fail(passed, failed, "tabs");
    }

    // array with spaces
    {
        auto *root = ParseJson("[ 1 , 2 , 3 ]"_s, alloc);
        bool ok = root && JsonValue::GetCount(*root) == 3;
        if (ok)
            Pass(passed, failed, "array with spaces");
        else
            Fail(passed, failed, "array with spaces");
    }
}

// ─── Edge cases ───────────────────────────────────────────────────────────────

void TestEdgeCases(region_alloc &alloc, int &passed, int &failed)
{
    printf("─── Edge cases ───\n");

    // large integer
    {
        auto *root = ParseJson("2147483647"_s, alloc);
        if (root && root->tag == json_tag::Integer && root->val.valInt == 2147483647)
            Pass(passed, failed, "max int32");
        else
            Fail(passed, failed, "max int32");
    }

    // negative zero
    {
        auto *root = ParseJson("-0"_s, alloc);
        if (root && root->tag == json_tag::Integer && root->val.valInt == 0)
            Pass(passed, failed, "negative zero");
        else
            Fail(passed, failed, "negative zero");
    }

    // mixed types in array
    {
        auto *root = ParseJson("[null, true, 1, \"s\"]"_s, alloc);
        bool ok = root && JsonValue::GetCount(*root) == 4;
        if (ok)
        {
            auto it = root->begin();
            ok = (*it).tag == json_tag::Null;
            ++it;
            ok = ok && (*it).tag == json_tag::Bool && (*it).val.valBool;
            ++it;
            ok = ok && (*it).tag == json_tag::Integer && (*it).val.valInt == 1;
            ++it;
            ok = ok && (*it).tag == json_tag::String && (*it).val.valStr.size == 1;
        }
        if (ok)
            Pass(passed, failed, "mixed array");
        else
            Fail(passed, failed, "mixed array");
    }

    // string with special chars (no escapes)
    {
        auto *root = ParseJson("\"hello world!\""_s, alloc);
        bool ok = root && root->tag == json_tag::String && root->val.valStr.size == 12;
        if (ok)
            Pass(passed, failed, "string with spaces");
        else
            Fail(passed, failed, "string with spaces");
    }

    // string with escaped quote
    {
        auto *root = ParseJson("\"hello\\\"world\""_s, alloc);
        // JSON: "hello\"world" — 12 raw bytes: hello\"world
        bool ok = root && root->tag == json_tag::String && root->val.valStr.size == 12;
        if (ok)
            Pass(passed, failed, "escaped quote");
        else
            Fail(passed, failed, "escaped quote");
    }

    // string with escaped backslash
    {
        auto *root = ParseJson("\"a\\\\b\""_s, alloc);
        // JSON: "a\\b" — 4 raw bytes: a, \, \, b
        bool ok = root && root->tag == json_tag::String && root->val.valStr.size == 4;
        if (ok)
            Pass(passed, failed, "escaped backslash");
        else
            Fail(passed, failed, "escaped backslash");
    }

    // string with escaped newline
    {
        auto *root = ParseJson("\"a\\nb\""_s, alloc);
        // JSON: "a\nb" — 4 raw bytes: a, \, n, b
        bool ok = root && root->tag == json_tag::String && root->val.valStr.size == 4;
        if (ok)
            Pass(passed, failed, "escaped newline");
        else
            Fail(passed, failed, "escaped newline");
    }

    // deeply nested (3 levels)
    {
        auto *root = ParseJson("{\"a\":{\"b\":{\"c\":1}}}"_s, alloc);
        bool ok = root && JsonValue::GetCount(*root) == 1;
        if (ok)
        {
            auto *inner = JsonValue::Object(*root, "a"_s);
            ok = inner && JsonValue::GetCount(*inner) == 1;
            if (ok)
            {
                inner = JsonValue::Object(*inner, "b"_s);
                ok = inner && JsonValue::GetCount(*inner) == 1;
                if (ok)
                {
                    auto d = JsonValue::DWord(*inner, "c"_s);
                    ok = (d == 1);
                }
            }
        }
        if (ok)
            Pass(passed, failed, "deeply nested 3 levels");
        else
            Fail(passed, failed, "deeply nested 3 levels");
    }
}

// ─── Error recovery ──────────────────────────────────────────────────────────

void TestErrorRecovery(region_alloc &alloc, int &passed, int &failed)
{
    printf("─── Error recovery ───\n");

    // Unclosed string
    {
        auto *root = ParseJson("\"unclosed"_s, alloc);
        if (!root)
            Pass(passed, failed, "unclosed string → nullptr");
        else
            Fail(passed, failed, "unclosed string → nullptr");
    }

    // Unknown literal
    {
        auto *root = ParseJson("xyz"_s, alloc);
        if (!root)
            Pass(passed, failed, "unknown literal → nullptr");
        else
            Fail(passed, failed, "unknown literal → nullptr");
    }

    // Truncated literal
    {
        auto *root = ParseJson("tru"_s, alloc);
        if (!root)
            Pass(passed, failed, "truncated 'tru' → nullptr");
        else
            Fail(passed, failed, "truncated 'tru' → nullptr");
    }

    // Unclosed array
    {
        auto *root = ParseJson("[1, 2"_s, alloc);
        if (!root)
            Pass(passed, failed, "unclosed array → nullptr");
        else
            Fail(passed, failed, "unclosed array → nullptr");
    }

    // Unclosed object
    {
        auto *root = ParseJson("{\"x\": 1"_s, alloc);
        if (!root)
            Pass(passed, failed, "unclosed object → nullptr");
        else
            Fail(passed, failed, "unclosed object → nullptr");
    }

    // Missing colon in object
    {
        auto *root = ParseJson("{\"x\" 1}"_s, alloc);
        if (!root)
            Pass(passed, failed, "missing colon → nullptr");
        else
            Fail(passed, failed, "missing colon → nullptr");
    }

    // Missing comma in object
    {
        auto *root = ParseJson("{\"x\": 1 \"y\": 2}"_s, alloc);
        if (!root)
            Pass(passed, failed, "missing comma → nullptr");
        else
            Fail(passed, failed, "missing comma → nullptr");
    }

    // Trailing comma in array (accepted — many parsers allow this)
    {
        auto *root = ParseJson("[1,]"_s, alloc);
        if (root)
            Pass(passed, failed, "trailing comma accepted");
        else
            Fail(passed, failed, "trailing comma accepted");
    }

    // Non-string key in object
    {
        auto *root = ParseJson("{1: 2}"_s, alloc);
        if (!root)
            Pass(passed, failed, "non-string key → nullptr");
        else
            Fail(passed, failed, "non-string key → nullptr");
    }

    // Garbage input
    {
        auto *root = ParseJson("not json at all"_s, alloc);
        if (!root)
            Pass(passed, failed, "garbage → nullptr");
        else
            Fail(passed, failed, "garbage → nullptr");
    }
}

// ─── gltf-style parsing ─────────────────────────────────────────────────────

void TestGltfStyle(region_alloc &alloc, int &passed, int &failed)
{
    printf("─── gltf-style parsing ───\n");

    // Minimal but realistic gltf 2.0 JSON
    const char *gltfJson = R"({
        "asset": {"version": "2.0", "generator": "test"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "Cube"}],
        "meshes": [{
            "primitives": [{
                "attributes": {"POSITION": 0, "NORMAL": 1},
                "indices": 2,
                "mode": 4
            }]
        }],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 24, "type": "VEC3"},
            {"bufferView": 1, "componentType": 5126, "count": 24, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5123, "count": 36, "type": "SCALAR"}
        ],
        "bufferViews": [
            {"buffer": 0, "byteLength": 288},
            {"buffer": 0, "byteOffset": 288, "byteLength": 288},
            {"buffer": 0, "byteOffset": 576, "byteLength": 72}
        ],
        "buffers": [{"byteLength": 648}]
    })";

    auto *root = ParseJson({(const uint8_t *)gltfJson, strlen(gltfJson)}, alloc);
    if (!root)
    {
        Fail(passed, failed, "parse root");
        return;
    }
    Pass(passed, failed, "parse root");

    // asset.version
    {
        auto *asset = JsonValue::Object(*root, "asset"_s);
        auto ver = JsonValue::String(*asset, "version"_s);
        bool ok = (ver.size == 3 && MemEq(ver.data, "2.0", 3));
        if (ok)
            Pass(passed, failed, "asset.version = 2.0");
        else
            Fail(passed, failed, "asset.version = 2.0");
    }

    // scene
    {
        auto scene = JsonValue::DWord(*root, "scene"_s);
        if (scene == 0)
            Pass(passed, failed, "scene = 0");
        else
            Fail(passed, failed, "scene = 0");
    }

    // scenes array
    {
        auto *scenes = JsonValue::Array(*root, "scenes"_s);
        bool ok = scenes && JsonValue::GetCount(*scenes) == 1;
        if (ok)
        {
            auto *scene0 = JsonValue::GetFront(*scenes);
            auto *nodes = JsonValue::Array(*scene0, "nodes"_s);
            ok = nodes && JsonValue::GetCount(*nodes) == 1;
        }
        if (ok)
            Pass(passed, failed, "scenes[0].nodes[0]");
        else
            Fail(passed, failed, "scenes[0].nodes[0]");
    }

    // meshes[0].primitives[0].attributes
    {
        auto *meshes = JsonValue::Array(*root, "meshes"_s);
        auto *mesh0 = JsonValue::GetFront(*meshes);
        auto *prims = JsonValue::Array(*mesh0, "primitives"_s);
        auto *prim0 = JsonValue::GetFront(*prims);
        auto *attrs = JsonValue::Object(*prim0, "attributes"_s);

        auto pos = JsonValue::DWord(*attrs, "POSITION"_s);
        auto nrm = JsonValue::DWord(*attrs, "NORMAL"_s);
        auto idx = JsonValue::DWord(*prim0, "indices"_s);
        auto mode = JsonValue::DWord(*prim0, "mode"_s);

        bool ok = (pos == 0 && nrm == 1 && idx == 2 && mode == 4);
        if (ok)
            Pass(passed, failed, "mesh primitive attributes");
        else
            Fail(passed, failed, "mesh primitive attributes");
    }

    // accessors iteration
    {
        auto *accessors = JsonValue::Array(*root, "accessors"_s);
        bool ok = (JsonValue::GetCount(*accessors) == 3);
        if (ok)
        {
            int i = 0;
            for (auto it = accessors->begin(), end = accessors->end(); it != end; ++it, ++i)
            {
                auto ct = JsonValue::DWord(*it, "componentType"_s);
                if (i == 0 && ct != 5126)
                    ok = false;
                if (i == 2 && ct != 5123)
                    ok = false;
            }
        }
        if (ok)
            Pass(passed, failed, "accessors iteration");
        else
            Fail(passed, failed, "accessors iteration");
    }

    // bufferViews with optional byteOffset (TryDWord)
    {
        auto *views = JsonValue::Array(*root, "bufferViews"_s);
        bool ok = (JsonValue::GetCount(*views) == 3);
        if (ok)
        {
            uint32_t offset0 = 0xFFFF;
            bool has0 = JsonValue::TryDWord(*JsonValue::GetFront(*views), "byteOffset"_s, offset0);
            ok = !has0; // first bufferView has no byteOffset

            auto it = views->begin();
            ++it; // second bufferView
            uint32_t offset1 = 0;
            bool has1 = JsonValue::TryDWord(*it, "byteOffset"_s, offset1);
            ok = ok && has1 && offset1 == 288;
        }
        if (ok)
            Pass(passed, failed, "bufferView optional byteOffset");
        else
            Fail(passed, failed, "bufferView optional byteOffset");
    }

    // buffers[0].byteLength
    {
        auto *buffers = JsonValue::Array(*root, "buffers"_s);
        auto len = JsonValue::DWord(*JsonValue::GetFront(*buffers), "byteLength"_s);
        if (len == 648)
            Pass(passed, failed, "buffer byteLength");
        else
            Fail(passed, failed, "buffer byteLength");
    }
}

} // namespace

void UserMain()
{
    region_alloc arena = RegionAlloc::Create(16 << 20, 0);

    int passed = 0, failed = 0;

    TestBasicTypes(arena, passed, failed);
    TestArrays(arena, passed, failed);
    TestObjects(arena, passed, failed);
    TestNavigation(arena, passed, failed);
    TestWhitespace(arena, passed, failed);
    TestEdgeCases(arena, passed, failed);
    TestGltfStyle(arena, passed, failed);
    TestErrorRecovery(arena, passed, failed);

    RegionAlloc::Destroy(arena);

    printf("\n═══════════════════════════════════════\n");
    printf("  Total: %d  Passed: %d  Failed: %d\n", passed + failed, passed, failed);
    printf("═══════════════════════════════════════\n");
}

} // namespace nyla
