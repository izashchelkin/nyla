// ─── Tool failure repair — intercept errors before they reach the main model ───
//
// Three-layer design:
//   Layer 0: Deterministic mechanical fixes (JSON truncation) — zero latency.
//   Layer 1: LLM repair or summarize via the main provider — clean context, good prompt.
//   Layer 2: Pass-through — the main model sees the raw error.

#include "coding_agent/tool_failure_repair.h"

#include "nyla/commons/fmt.h"
#include "nyla/commons/json_parser.h"
#include "nyla/commons/json_value.h"
#include "nyla/commons/mem.h"

namespace nyla
{

// ═══════════════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════════════

static auto AllocCopy(region_alloc &alloc, byteview src) -> byteview
{
    if (src.size == 0)
        return {};
    span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(alloc, src.size);
    MemCpy(buf.data, src.data, src.size);
    return {buf.data, src.size};
}

static auto Bv(const char *s) -> byteview
{
    uint32_t len = 0;
    while (s[len])
        ++len;
    return {(const uint8_t *)s, len};
}

static auto ByteviewEq(byteview a, byteview b) -> bool
{
    if (a.size != b.size)
        return false;
    for (uint32_t i = 0; i < a.size; ++i)
        if (a.data[i] != b.data[i])
            return false;
    return true;
}

static auto StartsWith(byteview haystack, byteview needle) -> bool
{
    if (haystack.size < needle.size)
        return false;
    for (uint32_t i = 0; i < needle.size; ++i)
        if (haystack.data[i] != needle.data[i])
            return false;
    return true;
}

static auto Contains(byteview haystack, const char *needle) -> bool
{
    uint32_t needleLen = 0;
    while (needle[needleLen])
        ++needleLen;
    if (haystack.size < needleLen)
        return false;
    for (uint32_t i = 0; i <= haystack.size - needleLen; ++i)
    {
        bool match = true;
        for (uint32_t j = 0; j < needleLen; ++j)
            if (haystack.data[i + j] != (uint8_t)needle[j])
                match = false;
        if (match)
            return true;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// Failure classification
// ═══════════════════════════════════════════════════════════════════════════

auto ClassifyToolFailure(byteview toolName, byteview errorContent) -> FailureLayer
{
    // ── Layer 2: Pass-through — errors the main model should see ──
    if (StartsWith(toolName, Bv("bash")))
    {
        if (Contains(errorContent, "ERROR: ") || Contains(errorContent, "missing required"))
            return FailureLayer::Semantic;
        return FailureLayer::PassThrough;
    }
    if (ByteviewEq(toolName, Bv("read_file")) || ByteviewEq(toolName, Bv("write_file")))
        return FailureLayer::PassThrough;

    // ── Layer 0: Mechanical — trivially fixable issues ──
    if (Contains(errorContent, "malformed tool_calls") || Contains(errorContent, "no name"))
        return FailureLayer::Mechanical;
    if (Contains(errorContent, "missing required argument"))
        return FailureLayer::Mechanical;

    // ── Layer 1: Semantic — needs LLM understanding ──
    if (StartsWith(toolName, Bv("xcav_")))
        return FailureLayer::Semantic;

    return FailureLayer::Semantic;
}

// ═══════════════════════════════════════════════════════════════════════════
// Layer 0: Deterministic mechanical repair
// ═══════════════════════════════════════════════════════════════════════════

static auto FixTruncatedJson(byteview original, region_alloc &alloc) -> byteview
{
    {
        span<json_value> storage = RegionAlloc::AllocArray<json_value>(alloc, 64);
        json_parser parser;
        JsonParser::Init(parser, original, storage);
        json_value *root = JsonParser::ParseNext(parser);
        if (root != nullptr)
            return {};
    }

    int braceDepth = 0;
    int bracketDepth = 0;
    bool inString = false;
    for (uint32_t i = 0; i < original.size; ++i)
    {
        uint8_t c = original.data[i];
        if (c == '\\' && inString)
        {
            ++i;
            continue;
        }
        if (c == '"')
        {
            inString = !inString;
            continue;
        }
        if (inString)
            continue;
        if (c == '{')
            ++braceDepth;
        else if (c == '}')
            --braceDepth;
        else if (c == '[')
            ++bracketDepth;
        else if (c == ']')
            --bracketDepth;
    }

    if (braceDepth <= 0 && bracketDepth <= 0)
        return {};

    uint32_t repairLen = original.size + (uint32_t)(braceDepth + bracketDepth);
    span<uint8_t> repaired = RegionAlloc::AllocArray<uint8_t>(alloc, repairLen);
    MemCpy(repaired.data, original.data, original.size);
    uint32_t pos = original.size;
    for (int i = 0; i < bracketDepth; ++i)
        repaired.data[pos++] = ']';
    for (int i = 0; i < braceDepth; ++i)
        repaired.data[pos++] = '}';

    {
        span<json_value> storage = RegionAlloc::AllocArray<json_value>(alloc, 128);
        json_parser parser;
        JsonParser::Init(parser, {repaired.data, pos}, storage);
        json_value *root = JsonParser::ParseNext(parser);
        if (root == nullptr)
            return {};
    }

    return {repaired.data, pos};
}

static auto FixMissingArg(byteview original, byteview errorContent, region_alloc &alloc) -> byteview
{
    if (!Contains(errorContent, "missing required argument"))
        return {};

    struct TypoFix
    {
        const char *wrong;
        const char *right;
    };
    const TypoFix kTypos[] = {
        {"scrFile", "srcFile"},
        {"destFile", "dstFile"},
        {"scrLine", "srcLine"},
    };

    for (uint32_t i = 0; i < (uint32_t)(sizeof(kTypos) / sizeof(kTypos[0])); ++i)
    {
        if (!Contains(original, kTypos[i].wrong))
            continue;
        if (!Contains(errorContent, kTypos[i].right))
            continue;

        uint32_t wrongLen = 0;
        while (kTypos[i].wrong[wrongLen])
            ++wrongLen;
        uint32_t rightLen = 0;
        while (kTypos[i].right[rightLen])
            ++rightLen;

        for (uint32_t j = 0; j + wrongLen <= original.size; ++j)
        {
            bool match = true;
            for (uint32_t k = 0; k < wrongLen; ++k)
                if (original.data[j + k] != (uint8_t)kTypos[i].wrong[k])
                    match = false;
            if (!match)
                continue;

            uint32_t fixedLen = original.size - wrongLen + rightLen;
            span<uint8_t> fixed = RegionAlloc::AllocArray<uint8_t>(alloc, fixedLen);
            MemCpy(fixed.data, original.data, j);
            MemCpy(fixed.data + j, kTypos[i].right, rightLen);
            MemCpy(fixed.data + j + rightLen, original.data + j + wrongLen, original.size - j - wrongLen);
            return {fixed.data, fixedLen};
        }
    }

    return {};
}

auto TryMechanicalRepair(byteview /*toolName*/, byteview originalArgs, byteview errorContent, region_alloc &alloc)
    -> byteview
{
    byteview fixed = FixTruncatedJson(originalArgs, alloc);
    if (fixed.size > 0)
        return fixed;

    fixed = FixMissingArg(originalArgs, errorContent, alloc);
    if (fixed.size > 0)
        return fixed;

    return {};
}

// ═══════════════════════════════════════════════════════════════════════════
// Layer 1: LLM repair via the main provider (clean context, good prompt)
// ═══════════════════════════════════════════════════════════════════════════

// Parse the helper model's text response to extract the repair action JSON.
static auto ParseHelperText(byteview text, region_alloc &alloc, byteview &action, byteview &fixedArgs,
                            byteview &summary) -> bool
{
    if (text.size == 0)
        return false;

    // Strip leading whitespace and markdown fences.
    byteview jsonStart = text;
    while (jsonStart.size > 0 && (jsonStart.data[0] == ' ' || jsonStart.data[0] == '\n' || jsonStart.data[0] == '\t' ||
                                  jsonStart.data[0] == '\r'))
    {
        jsonStart.data++;
        jsonStart.size--;
    }

    if (StartsWith(jsonStart, Bv("```json")))
    {
        uint32_t skip = 7;
        if (skip < jsonStart.size && jsonStart.data[skip] == '\n')
            ++skip;
        jsonStart.data += skip;
        jsonStart.size -= skip;
        for (uint32_t i = 0; i + 3 <= jsonStart.size; ++i)
        {
            if (jsonStart.data[i] == '`' && jsonStart.data[i + 1] == '`' && jsonStart.data[i + 2] == '`')
            {
                jsonStart.size = i;
                break;
            }
        }
    }
    else if (StartsWith(jsonStart, Bv("```")))
    {
        uint32_t skip = 3;
        if (skip < jsonStart.size && jsonStart.data[skip] == '\n')
            ++skip;
        jsonStart.data += skip;
        jsonStart.size -= skip;
        for (uint32_t i = 0; i + 3 <= jsonStart.size; ++i)
        {
            if (jsonStart.data[i] == '`' && jsonStart.data[i + 1] == '`' && jsonStart.data[i + 2] == '`')
            {
                jsonStart.size = i;
                break;
            }
        }
    }

    // Find outermost { ... }
    uint32_t objStart = 0;
    while (objStart < jsonStart.size && jsonStart.data[objStart] != '{')
        ++objStart;
    if (objStart >= jsonStart.size)
        return false;

    uint32_t objEnd = objStart;
    int depth = 0;
    bool inStr = false;
    for (uint32_t i = objStart; i < jsonStart.size; ++i)
    {
        uint8_t c = jsonStart.data[i];
        if (c == '\\' && inStr)
        {
            ++i;
            continue;
        }
        if (c == '"')
        {
            inStr = !inStr;
            continue;
        }
        if (inStr)
            continue;
        if (c == '{')
            ++depth;
        else if (c == '}')
        {
            --depth;
            if (depth == 0)
            {
                objEnd = i + 1;
                break;
            }
        }
    }

    byteview jsonObj = {jsonStart.data + objStart, objEnd - objStart};

    span<json_value> storage = RegionAlloc::AllocArray<json_value>(alloc, 256);
    json_parser parser;
    JsonParser::Init(parser, jsonObj, storage);
    json_value *root = JsonParser::ParseNext(parser);
    if (!root || root->tag != json_tag::ObjectBegin)
        return false;

    if (!JsonValue::TryString(*root, "action"_s, action))
        return false;

    if (ByteviewEq(action, Bv("retry")))
    {
        json_value *argsObj;
        if (JsonValue::TryObject(*root, "fixedArgs"_s, argsObj))
        {
            constexpr uint64_t kArgBuf = 4096;
            span<uint8_t> ab = RegionAlloc::AllocArray<uint8_t>(alloc, kArgBuf);
            uint64_t ap = 0;
            ab.data[ap++] = '{';
            json_value *children = JsonValue::GetFront(*argsObj);
            uint32_t pairCount = JsonValue::GetCount(*argsObj);
            bool first = true;
            for (uint32_t ci = 0; ci < pairCount; ++ci)
            {
                json_value *k = children + ci * 2;
                json_value *v = children + ci * 2 + 1;
                if (!k || !v || k->tag != json_tag::String)
                    continue;
                if (!first && ap + 1 < kArgBuf)
                    ab.data[ap++] = ',';
                first = false;
                if (ap + k->val.valStr.size + 3 < kArgBuf)
                {
                    ab.data[ap++] = '"';
                    MemCpy(ab.data + ap, k->val.valStr.data, k->val.valStr.size);
                    ap += k->val.valStr.size;
                    ab.data[ap++] = '"';
                    ab.data[ap++] = ':';
                }
                if (v->tag == json_tag::String)
                {
                    if (ap + v->val.valStr.size + 3 < kArgBuf)
                    {
                        ab.data[ap++] = '"';
                        MemCpy(ab.data + ap, v->val.valStr.data, v->val.valStr.size);
                        ap += v->val.valStr.size;
                        ab.data[ap++] = '"';
                    }
                }
                else if (v->tag == json_tag::Integer)
                {
                    uint8_t nb[32];
                    uint32_t nl = StringWriteFmt(span<uint8_t>{nb, sizeof(nb)}, "%lld"_s, (long long)v->val.valInt);
                    if (ap + nl < kArgBuf)
                    {
                        MemCpy(ab.data + ap, nb, nl);
                        ap += nl;
                    }
                }
                else if (v->tag == json_tag::Bool)
                {
                    const char *bv = v->val.valBool ? "true" : "false";
                    uint32_t bl = v->val.valBool ? 4 : 5;
                    if (ap + bl < kArgBuf)
                    {
                        MemCpy(ab.data + ap, bv, bl);
                        ap += bl;
                    }
                }
            }
            if (ap < kArgBuf)
                ab.data[ap++] = '}';
            fixedArgs = {ab.data, (uint32_t)ap};
            return true;
        }
        if (JsonValue::TryString(*root, "fixedArgs"_s, fixedArgs))
            return true;
        return false;
    }

    if (ByteviewEq(action, Bv("summarize")))
    {
        if (!JsonValue::TryString(*root, "summary"_s, summary))
            return false;
        return true;
    }

    return false;
}

auto TryLLMRepair(byteview toolName, byteview originalArgs, byteview errorContent, AgentConfig const &config,
                  ProviderFn provider, region_alloc &alloc) -> ToolResult
{
    ToolResult fallback;
    fallback.isError = true;
    fallback.content = errorContent;

    // ── System prompt for the helper ─────────────────────────────────
    const char *kHelperSystemPrompt =
        "You are a tool-call repair assistant. You receive a failed tool invocation. "
        "Either fix the arguments or summarize the failure. Respond with JSON only:\n"
        "  {\"action\": \"retry\", \"fixedArgs\": {\"arg1\": \"val1\", ...}}\n"
        "  or\n"
        "  {\"action\": \"summarize\", \"summary\": \"Brief explanation and suggestion.\"}\n"
        "Rules:\n"
        "- Use \"retry\" only if you are confident the fix is correct.\n"
        "- Use \"summarize\" if the error is ambiguous or unfixable.\n"
        "- Keep summaries concise (1-2 sentences).\n"
        "- Do NOT call any tools. Respond with text only.";

    // ── Build user message with failure context ──────────────────────
    uint8_t userBuf[4096];
    uint32_t userLen = StringWriteFmt(span<uint8_t>{userBuf, sizeof(userBuf)},
                                      "Tool call failed:\n"
                                      "  Tool: %.*s\n"
                                      "  Arguments: %.*s\n"
                                      "  Error: %.*s\n"
                                      "\nFix the arguments or summarize this failure."_s,
                                      (int)toolName.size, toolName.data, (int)originalArgs.size, originalArgs.data,
                                      (int)errorContent.size, errorContent.data);

    // ── Build minimal conversation ────────────────────────────────────
    Conversation helperConv{};
    {
        Message sysMsg{};
        sysMsg.role = MessageRole::System;
        sysMsg.content = Bv(kHelperSystemPrompt);
        InlineVec::Append(helperConv, sysMsg);
    }
    {
        Message userMsg{};
        userMsg.role = MessageRole::User;
        userMsg.content = {userBuf, userLen};
        InlineVec::Append(helperConv, userMsg);
    }

    // ── Use helper model name ──────────────────────────────────────────
    AgentConfig helperConfig = config;
    helperConfig.modelName = config.helperModel;
    helperConfig.maxTokens = 512;
    helperConfig.temperature = 0.0f;

    // ── Call the provider ──────────────────────────────────────────────
    ProviderResponse response = provider(helperConv, helperConfig, alloc);
    if (response.finishReason == ProviderFinishReason::Error || response.content.size == 0)
        return fallback;

    // ── Parse the repair JSON from the text response ───────────────────
    byteview action{};
    byteview fixedArgs{};
    byteview summary{};

    if (!ParseHelperText(response.content, alloc, action, fixedArgs, summary))
        return fallback;

    if (ByteviewEq(action, Bv("summarize")))
    {
        uint8_t buf[4096];
        uint32_t len = StringWriteFmt(span<uint8_t>{buf, sizeof(buf)},
                                      "[helper model summary] %.*s\n"
                                      "(Original error: %.*s)"_s,
                                      (int)summary.size, summary.data, (int)errorContent.size, errorContent.data);
        fallback.content = AllocCopy(alloc, {buf, len});
        return fallback;
    }

    // ── "retry" action — signal the caller to re-dispatch ──────────────
    ToolResult repaired;
    repaired.isError = false;
    uint8_t resultBuf[8192];
    uint32_t resultLen = StringWriteFmt(span<uint8_t>{resultBuf, sizeof(resultBuf)}, "[REPAIR_RETRY]%.*s"_s,
                                        (int)fixedArgs.size, fixedArgs.data);
    repaired.content = AllocCopy(alloc, {resultBuf, resultLen});
    return repaired;
}

} // namespace nyla
