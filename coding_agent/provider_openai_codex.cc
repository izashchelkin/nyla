// ─── OpenAI Codex (ChatGPT subscription) provider ────────────────────────────
// Uses the Codex Responses API at chatgpt.com/backend-api/codex/responses.
// Streaming is mandatory — we capture the full SSE response and parse it.

#include "coding_agent/provider_openai_codex.h"
#include "coding_agent/provider.h"

#include <cstdio>
#include <cstring>

#include "nyla/commons/file.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/json_parser.h"
#include "nyla/commons/json_value.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/subprocess.h"

#include "coding_agent/tool_registry.h"

namespace nyla
{

// ─── Constants ──────────────────────────────────────────────────────────────

constexpr uint64_t kMaxBodySize = 512 * 1024;
constexpr uint64_t kMaxResponseSize = 2 * 1024 * 1024; // 2 MB max SSE response
constexpr uint32_t kTimeoutMs = 120'000;
constexpr uint32_t kSseParseJsonStorage = 8192;

// ─── Helpers ────────────────────────────────────────────────────────────────

static auto AllocAndCopy(region_alloc &alloc, byteview src) -> byteview
{
    if (src.size == 0)
        return {};
    span<uint8_t> dst = RegionAlloc::AllocArray<uint8_t>(alloc, src.size);
    MemCpy(dst.data, src.data, src.size);
    return {dst.data, src.size};
}

static auto ByteviewFromCStr(const char *s) -> byteview
{
    uint32_t len = 0;
    while (s[len])
        ++len;
    return {(const uint8_t *)s, len};
}

// ─── JSON escaping ──────────────────────────────────────────────────────────

static auto JsonEscapeString(span<uint8_t> dst, byteview src) -> uint64_t
{
    uint64_t out = 0;
    for (uint64_t i = 0; i < src.size; ++i)
    {
        uint8_t c = src.data[i];
        switch (c)
        {
        case '"':
            if (out + 2 <= dst.size)
            {
                dst.data[out] = '\\';
                dst.data[out + 1] = '"';
            }
            out += 2;
            break;
        case '\\':
            if (out + 2 <= dst.size)
            {
                dst.data[out] = '\\';
                dst.data[out + 1] = '\\';
            }
            out += 2;
            break;
        case '\n':
            if (out + 2 <= dst.size)
            {
                dst.data[out] = '\\';
                dst.data[out + 1] = 'n';
            }
            out += 2;
            break;
        case '\r':
            if (out + 2 <= dst.size)
            {
                dst.data[out] = '\\';
                dst.data[out + 1] = 'r';
            }
            out += 2;
            break;
        case '\t':
            if (out + 2 <= dst.size)
            {
                dst.data[out] = '\\';
                dst.data[out + 1] = 't';
            }
            out += 2;
            break;
        default:
            if (out < dst.size)
                dst.data[out] = c;
            out++;
            break;
        }
    }
    return out;
}

// ─── Buffer builder ─────────────────────────────────────────────────────────

struct BufferBuilder
{
    span<uint8_t> buf;
    uint64_t pos = 0;

    auto WriteRaw(const char *s, uint64_t len) -> bool
    {
        if (pos + len > buf.size)
            return false;
        MemCpy(buf.data + pos, s, len);
        pos += len;
        return true;
    }

    auto WriteRaw(const char *s) -> bool
    {
        return WriteRaw(s, strlen(s));
    }

    auto WriteRaw(byteview src) -> bool
    {
        return WriteRaw((const char *)src.data, src.size);
    }

    auto WriteJsonString(byteview content) -> bool
    {
        if (pos >= buf.size)
            return false;
        buf.data[pos] = '"';
        pos++;

        span<uint8_t> escapeBuf{buf.data + pos, buf.size - pos - 1};
        uint64_t escaped = JsonEscapeString(escapeBuf, content);
        pos += escaped;

        if (pos >= buf.size)
            return false;
        buf.data[pos] = '"';
        pos++;
        return true;
    }

    auto WriteUint(uint32_t val) -> bool
    {
        uint8_t numBuf[32];
        uint32_t numLen = StringWriteFmt(span<uint8_t>{numBuf, sizeof(numBuf)}, "%u"_s, val);
        return WriteRaw((const char *)numBuf, numLen);
    }
};

// ─── Build request body (Codex Responses API format) ────────────────────────

static auto BuildChatRequestBody(Conversation const &conv, AgentConfig const &config, region_alloc &alloc) -> byteview
{
    span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(alloc, kMaxBodySize);
    BufferBuilder bb{buf};

    bb.WriteRaw("{\"model\":\"");
    bb.WriteRaw(config.modelName);
    bb.WriteRaw("\",\"store\":false,\"stream\":true");

    // Instructions (system prompt) — first message if role is System
    if (conv.size > 0 && conv.data[0].role == MessageRole::System && conv.data[0].content.size > 0)
    {
        bb.WriteRaw(",\"instructions\":");
        bb.WriteJsonString(conv.data[0].content);
    }

    // Input array (messages + tool calls/results, skipping system prompt)
    bb.WriteRaw(",\"input\":[");
    bool first = true;
    for (uint64_t i = 0; i < conv.size; ++i)
    {
        Message const &msg = conv.data[i];
        if (msg.role == MessageRole::System)
            continue;

        if (!first)
            bb.WriteRaw(",");
        first = false;

        if (msg.role == MessageRole::User)
        {
            bb.WriteRaw("{\"role\":\"user\",\"content\":");
            bb.WriteJsonString(msg.content);
            bb.WriteRaw("}");
        }
        else if (msg.role == MessageRole::Assistant && msg.toolCallsJson.size > 0)
        {
            // Parse tool_calls JSON and emit function_call items
            span<json_value> tcStorage = RegionAlloc::AllocArray<json_value>(alloc, 256);
            json_parser tcParser;
            JsonParser::Init(tcParser, msg.toolCallsJson, tcStorage);
            json_value *tcRoot = JsonParser::ParseNext(tcParser);

            bool tcFirst = true;
            if (tcRoot && tcRoot->tag == json_tag::ArrayBegin)
            {
                for (auto &tc : *tcRoot)
                {
                    if (tc.tag != json_tag::ObjectBegin)
                        continue;
                    byteview callId = JsonValue::String(tc, "id"_s);
                    byteview fnName;
                    byteview fnArgs;
                    json_value *fn = JsonValue::Object(tc, "function"_s);
                    if (fn)
                    {
                        fnName = JsonValue::String(*fn, "name"_s);
                        fnArgs = JsonValue::String(*fn, "arguments"_s);
                    }

                    if (!tcFirst)
                        bb.WriteRaw(",");
                    tcFirst = false;

                    bb.WriteRaw("{\"type\":\"function_call\",\"call_id\":");
                    bb.WriteJsonString(callId);
                    bb.WriteRaw(",\"name\":");
                    bb.WriteJsonString(fnName);
                    bb.WriteRaw(",\"arguments\":");
                    bb.WriteJsonString(fnArgs);
                    bb.WriteRaw("}");
                }
            }
        }
        else if (msg.role == MessageRole::Tool)
        {
            bb.WriteRaw("{\"type\":\"function_call_output\",\"call_id\":");
            bb.WriteJsonString(msg.toolCallId);
            bb.WriteRaw(",\"output\":");
            bb.WriteJsonString(msg.content);
            bb.WriteRaw("}");
        }
        else if (msg.role == MessageRole::Assistant && msg.content.size > 0)
        {
            bb.WriteRaw("{\"role\":\"assistant\",\"content\":");
            bb.WriteJsonString(msg.content);
            bb.WriteRaw("}");
        }
        else
        {
            // Empty assistant message — skip
            // Back up the comma
            if (first)
                first = true;
            else
                bb.pos -= 1; // undo comma
        }
    }
    bb.WriteRaw("]");

    // Tools
    span<const ToolDef> tools = GetToolDefs();
    if (tools.size > 0)
    {
        bb.WriteRaw(",\"tools\":[");
        for (uint64_t i = 0; i < tools.size; ++i)
        {
            ToolDef const &tool = tools.data[i];
            if (i > 0)
                bb.WriteRaw(",");
            bb.WriteRaw("{\"type\":\"function\",\"name\":\"");
            bb.WriteRaw(tool.name);
            bb.WriteRaw("\",\"description\":");
            bb.WriteJsonString(ByteviewFromCStr(tool.description));
            bb.WriteRaw(",\"parameters\":");
            bb.WriteRaw(tool.parametersJson);
            bb.WriteRaw("}");
        }
        bb.WriteRaw("]");
    }
    bb.WriteRaw(",\"tool_choice\":\"auto\"");
    bb.WriteRaw(",\"parallel_tool_calls\":true");

    // Reasoning (enables thinking/reasoning tokens in SSE stream)
    bb.WriteRaw(",\"reasoning\":{\"effort\":\"medium\",\"summary\":\"auto\"}");

    // Text verbosity
    bb.WriteRaw(",\"text\":{\"verbosity\":\"low\"}");

    // Temperature not supported by this model -- skip

    bb.WriteRaw("}");
    return {buf.data, bb.pos};
}

// ─── SSE parsing ────────────────────────────────────────────────────────────

// Find the next SSE event boundary (\n\n). Returns {start, length} of one event
// (excluding the trailing \n\n). Returns {0, 0} when no more events.
static auto NextSseEvent(byteview body, uint64_t &offset) -> byteview
{
    if (offset >= body.size)
        return {};

    uint64_t start = offset;
    uint64_t end = start;

    while (end + 1 < body.size)
    {
        if (body.data[end] == '\n' && body.data[end + 1] == '\n')
        {
            byteview event = {body.data + start, end - start};
            offset = end + 2;
            return event;
        }
        ++end;
    }

    // Last event (no trailing \n\n at EOF)
    byteview event = {body.data + start, body.size - start};
    offset = body.size;
    return event;
}

// Extract "data:" lines from an SSE event, concatenating multi-line data.
// Returns the JSON content (without "data: " prefix).
static auto ExtractSseData(byteview event, region_alloc &alloc) -> byteview
{
    // Find all "data:" lines and concatenate their content
    uint64_t totalLen = 0;
    uint64_t pos = 0;

    // First pass: compute total length
    {
        uint64_t p = 0;
        while (p < event.size)
        {
            // Find start of line
            uint64_t lineStart = p;
            while (lineStart < event.size && (event.data[lineStart] == '\n' || event.data[lineStart] == '\r'))
                ++lineStart;
            if (lineStart >= event.size)
                break;

            uint64_t lineEnd = lineStart;
            while (lineEnd < event.size && event.data[lineEnd] != '\n' && event.data[lineEnd] != '\r')
                ++lineEnd;

            byteview line = {event.data + lineStart, lineEnd - lineStart};
            if (line.size > 5 && line.data[0] == 'd' && line.data[1] == 'a' && line.data[2] == 't' &&
                line.data[3] == 'a' && line.data[4] == ':')
            {
                // Skip "data:" and optional space
                uint64_t dataStart = 5;
                if (dataStart < line.size && line.data[dataStart] == ' ')
                    ++dataStart;
                totalLen += (line.size - dataStart);
            }

            p = lineEnd;
        }
    }

    if (totalLen == 0)
        return {};

    span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(alloc, totalLen);

    // Second pass: copy data
    pos = 0;
    {
        uint64_t p = 0;
        while (p < event.size)
        {
            uint64_t lineStart = p;
            while (lineStart < event.size && (event.data[lineStart] == '\n' || event.data[lineStart] == '\r'))
                ++lineStart;
            if (lineStart >= event.size)
                break;

            uint64_t lineEnd = lineStart;
            while (lineEnd < event.size && event.data[lineEnd] != '\n' && event.data[lineEnd] != '\r')
                ++lineEnd;

            byteview line = {event.data + lineStart, lineEnd - lineStart};
            if (line.size > 5 && line.data[0] == 'd' && line.data[1] == 'a' && line.data[2] == 't' &&
                line.data[3] == 'a' && line.data[4] == ':')
            {
                uint64_t dataStart = 5;
                if (dataStart < line.size && line.data[dataStart] == ' ')
                    ++dataStart;
                uint64_t dataLen = line.size - dataStart;
                if (dataLen > 0)
                {
                    MemCpy(buf.data + pos, line.data + dataStart, dataLen);
                    pos += dataLen;
                }
            }

            p = lineEnd;
        }
    }

    return {buf.data, pos};
}

// ─── Parse SSE events into a ProviderResponse ───────────────────────────────
static auto ParseSseResponse(byteview body, region_alloc &alloc) -> ProviderResponse
{
    ProviderResponse result;
    result.finishReason = ProviderFinishReason::Error;

    if (body.size == 0)
    {
        const char errMsg[] = "Empty response from provider.";
        result.content = AllocAndCopy(alloc, ByteviewFromCStr(errMsg));
        return result;
    }

    // Accumulated text content (from output_text.delta events)
    uint8_t textBuf[65536];
    uint32_t textLen = 0;

    // Accumulated thinking/reasoning text (from reasoning_text.delta events)
    uint8_t thinkingBuf[65536];
    uint32_t thinkingLen = 0;

    // Tool calls: collect call_id, name, arguments
    struct ToolCallInfo
    {
        byteview callId;
        byteview name;
        byteview arguments;
    };
    inline_vec<ToolCallInfo, 16> toolCalls;
    InlineVec::Clear(toolCalls);

    // Current tool call being accumulated
    byteview currentCallId{};
    byteview currentName{};
    bool hasCompleted = false;
    bool hasError = false;
    uint32_t totalTokens = 0;
    byteview errorMessage{};

    uint64_t offset = 0;
    while (offset < body.size)
    {
        byteview event = NextSseEvent(body, offset);
        if (event.size == 0)
            break;

        byteview data = ExtractSseData(event, alloc);
        if (data.size == 0)
            continue;

        // Parse the JSON
        span<json_value> jsonStorage = RegionAlloc::AllocArray<json_value>(alloc, kSseParseJsonStorage);
        json_parser parser;
        JsonParser::Init(parser, data, jsonStorage);
        json_value *root = JsonParser::ParseNext(parser);
        if (!root || root->tag != json_tag::ObjectBegin)
            continue;

        // Check event type
        byteview eventType = JsonValue::String(*root, "type"_s);

        // response.reasoning_text.delta — accumulate thinking/reasoning text
        if (ByteviewEq(eventType, "response.reasoning_text.delta"))
        {
            byteview delta = JsonValue::String(*root, "delta"_s);
            if (delta.size > 0)
            {
                span<uint8_t> unesc = RegionAlloc::AllocArray<uint8_t>(alloc, delta.size);
                byteview unescaped = UnescapeJsonString(delta, unesc);
                if (unescaped.size > 0 && thinkingLen + unescaped.size < sizeof(thinkingBuf))
                {
                    MemCpy(thinkingBuf + thinkingLen, unescaped.data, unescaped.size);
                    thinkingLen += (uint32_t)unescaped.size;
                }
            }
        }

        // response.output_text.delta — accumulate text
        if (ByteviewEq(eventType, "response.output_text.delta"))
        {
            byteview delta = JsonValue::String(*root, "delta"_s);
            if (delta.size > 0)
            {
                // Unescape JSON escape sequences in delta text
                span<uint8_t> unesc = RegionAlloc::AllocArray<uint8_t>(alloc, delta.size);
                byteview unescaped = UnescapeJsonString(delta, unesc);
                if (unescaped.size > 0 && textLen + unescaped.size < sizeof(textBuf))
                {
                    MemCpy(textBuf + textLen, unescaped.data, unescaped.size);
                    textLen += (uint32_t)unescaped.size;
                }
            }
        }

        // response.output_item.added — check for function_call
        if (ByteviewEq(eventType, "response.output_item.added"))
        {
            json_value *item = JsonValue::Object(*root, "item"_s);
            if (item)
            {
                byteview itemType = JsonValue::String(*item, "type"_s);
                if (ByteviewEq(itemType, "function_call"))
                {
                    currentCallId = JsonValue::String(*item, "call_id"_s);
                    currentName = JsonValue::String(*item, "name"_s);
                }
            }
        }

        // response.function_call_arguments.done — final args for current tool call
        if (ByteviewEq(eventType, "response.function_call_arguments.done"))
        {
            byteview args = JsonValue::String(*root, "arguments"_s);
            if (currentCallId.size > 0)
            {
                ToolCallInfo tc;
                tc.callId = AllocAndCopy(alloc, currentCallId);
                tc.name = AllocAndCopy(alloc, currentName);
                tc.arguments = AllocAndCopy(alloc, args);
                InlineVec::Append(toolCalls, tc);
                currentCallId = {};
                currentName = {};
            }
        }

        // response.output_item.done -- check for final text from message items
        if (ByteviewEq(eventType, "response.output_item.done"))
        {
            json_value *item = JsonValue::Object(*root, "item"_s);
            if (item)
            {
                byteview itemType = JsonValue::String(*item, "type"_s);
                if (ByteviewEq(itemType, "message"))
                {
                    // Extract text from content[0].text as fallback
                    json_value *content = JsonValue::Array(*item, "content"_s);
                    if (content && JsonValue::GetCount(*content) > 0)
                    {
                        json_value &firstContent = *(JsonValue::GetFront(*content));
                        byteview fullText = JsonValue::String(firstContent, "text"_s);
                        if (fullText.size > 0 && textLen == 0)
                        {
                            // Only use if we didn't get deltas (some responses skip deltas)
                            span<uint8_t> unesc = RegionAlloc::AllocArray<uint8_t>(alloc, fullText.size);
                            byteview unescaped = UnescapeJsonString(fullText, unesc);
                            uint32_t copyLen = unescaped.size < sizeof(textBuf) ? (uint32_t)unescaped.size
                                                                                : (uint32_t)(sizeof(textBuf) - 1);
                            MemCpy(textBuf, unescaped.data, copyLen);
                            textLen = copyLen;
                        }
                    }
                }
            }
        }

        // response.completed -- final event
        if (ByteviewEq(eventType, "response.completed"))
        {
            hasCompleted = true;
            json_value *response = JsonValue::Object(*root, "response"_s);
            if (response)
            {
                // Check status
                byteview status = JsonValue::String(*response, "status"_s);
                if (ByteviewEq(status, "failed"))
                {
                    hasError = true;
                    json_value *err = JsonValue::Object(*response, "error"_s);
                    if (err)
                        errorMessage = JsonValue::String(*err, "message"_s);
                }

                // Usage
                json_value *usage = JsonValue::Object(*response, "usage"_s);
                if (usage)
                    JsonValue::TryDWord(*usage, "total_tokens"_s, totalTokens);
            }
        }

        // response.failed — error
        if (ByteviewEq(eventType, "response.failed"))
        {
            hasError = true;
            json_value *response = JsonValue::Object(*root, "response"_s);
            if (response)
            {
                json_value *err = JsonValue::Object(*response, "error"_s);
                if (err)
                    errorMessage = JsonValue::String(*err, "message"_s);
            }
        }

        // error event
        if (ByteviewEq(eventType, "error"))
        {
            hasError = true;
            byteview msg = JsonValue::String(*root, "message"_s);
            if (msg.size > 0)
                errorMessage = msg;
        }
    }

    if (hasError)
    {
        result.finishReason = ProviderFinishReason::Error;
        if (errorMessage.size > 0)
        {
            span<uint8_t> unesc = RegionAlloc::AllocArray<uint8_t>(alloc, errorMessage.size);
            byteview unescaped = UnescapeJsonString(errorMessage, unesc);
            uint8_t errBuf[512];
            uint64_t errLen =
                StringWriteFmt(span<uint8_t>{errBuf, sizeof(errBuf)}, "Codex error: %.*s"_s, SV_ARG(unescaped));
            result.content = AllocAndCopy(alloc, {errBuf, errLen});
        }
        else
        {
            result.content = AllocAndCopy(alloc, ByteviewFromCStr("Codex returned an error."));
        }
        return result;
    }

    if (!hasCompleted)
    {
        // If we got text from deltas, return it as Stop (partial success).
        // Otherwise report error.
        if (textLen > 0 || toolCalls.size > 0)
        {
            // Fall through to normal processing with what we have
            hasCompleted = true;
        }
        else
        {
            // Debug: include raw body prefix to diagnose parsing failures
            uint64_t bodyPrefix = body.size < 500 ? body.size : 500;
            uint8_t errBuf[1024];
            uint64_t errLen = StringWriteFmt(span<uint8_t>{errBuf, sizeof(errBuf)},
                                             "Response stream ended without completion event (textLen=%u "
                                             "thinkingLen=%u toolCalls=%u bodySize=%llu). "
                                             "Raw body prefix: %.*s"_s,
                                             textLen, thinkingLen, (uint32_t)toolCalls.size,
                                             (unsigned long long)body.size, (int)bodyPrefix, body.data);
            result.finishReason = ProviderFinishReason::Error;
            result.content = AllocAndCopy(alloc, {errBuf, errLen});
            return result;
        }
    }
    result.totalTokens = totalTokens;

    // Determine finish reason: tool calls present → ToolCalls, otherwise Stop
    if (toolCalls.size > 0)
    {
        result.finishReason = ProviderFinishReason::ToolCalls;

        // Serialize tool calls to JSON array (same format as OpenAI)
        constexpr uint64_t kTcBufSize = 16384;
        span<uint8_t> tcBuf = RegionAlloc::AllocArray<uint8_t>(alloc, kTcBufSize);
        BufferBuilder tcBB{tcBuf};

        tcBB.WriteRaw("[");
        for (uint64_t i = 0; i < toolCalls.size; ++i)
        {
            if (i > 0)
                tcBB.WriteRaw(",");
            tcBB.WriteRaw("{\"id\":");
            tcBB.WriteJsonString(toolCalls.data[i].callId);
            tcBB.WriteRaw(",\"type\":\"function\",\"function\":{\"name\":");
            tcBB.WriteJsonString(toolCalls.data[i].name);
            tcBB.WriteRaw(",\"arguments\":\"");
            // Write arguments as raw bytes -- they're already JSON-escaped from the Codex API.
            // Don't use WriteJsonString which would double-escape.
            tcBB.WriteRaw(toolCalls.data[i].arguments);
            tcBB.WriteRaw("\"}}");
        }
        tcBB.WriteRaw("]");

        result.toolCallsJson = {tcBuf.data, tcBB.pos};
    }
    else
    {
        result.finishReason = ProviderFinishReason::Stop;
        if (thinkingLen > 0)
            result.thinkingContent = AllocAndCopy(alloc, {thinkingBuf, thinkingLen});
        if (textLen > 0)
            result.content = AllocAndCopy(alloc, {textBuf, textLen});
    }

    return result;
}

// ─── Extract account ID from JWT ────────────────────────────────────────────

static auto ExtractAccountId(byteview token, region_alloc &alloc) -> byteview
{
    // JWT is header.payload.signature. Account ID is in payload.https://api.openai.com/auth.chatgpt_account_id
    // We need to base64-decode the payload and parse JSON. For simplicity, we'll
    // do a minimal scan for "chatgpt_account_id" in the raw token.

    // Find the second dot (start of payload)
    uint64_t dot1 = 0;
    while (dot1 < token.size && token.data[dot1] != '.')
        ++dot1;
    if (dot1 >= token.size)
        return {};

    uint64_t dot2 = dot1 + 1;
    while (dot2 < token.size && token.data[dot2] != '.')
        ++dot2;

    byteview payloadB64 = {token.data + dot1 + 1, dot2 - dot1 - 1};
    if (payloadB64.size == 0)
        return {};

    // Base64url decode (no padding, URL-safe chars)
    span<uint8_t> decoded = RegionAlloc::AllocArray<uint8_t>(alloc, payloadB64.size);
    uint32_t decodedLen = 0;

    // Base64url lookup table
    static const int8_t kDecode[256] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, 52, 53, 54, 55,
        56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
        13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, 63, -1, 26, 27, 28, 29, 30, 31, 32,
        33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
    };

    uint32_t buf = 0;
    int bits = 0;
    for (uint64_t i = 0; i < payloadB64.size; ++i)
    {
        uint8_t c = payloadB64.data[i];
        if (c == '=' || c == '\n' || c == '\r')
            break;
        if (c == '-' || c == '_')
            c = (c == '-') ? '+' : '/';
        int8_t val = kDecode[c];
        if (val < 0)
            continue;
        buf = (buf << 6) | (uint32_t)val;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            decoded.data[decodedLen++] = (uint8_t)(buf >> bits);
            buf &= (1u << bits) - 1;
        }
    }

    // Scan decoded payload for "chatgpt_account_id":"<uuid>"
    byteview decodedView{decoded.data, decodedLen};
    const char kKey[] = "\"chatgpt_account_id\":\"";
    uint32_t keyLen = (uint32_t)(sizeof(kKey) - 1);

    for (uint64_t i = 0; i + keyLen < decodedView.size; ++i)
    {
        bool match = true;
        for (uint32_t j = 0; j < keyLen; ++j)
        {
            if (decodedView.data[i + j] != (uint8_t)kKey[j])
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            uint64_t valStart = i + keyLen;
            uint64_t valEnd = valStart;
            while (valEnd < decodedView.size && decodedView.data[valEnd] != '"')
                ++valEnd;
            if (valEnd > valStart)
                return AllocAndCopy(alloc, {decodedView.data + valStart, valEnd - valStart});
        }
    }

    return {};
}

// ─── Public API ─────────────────────────────────────────────────────────────
auto OpenAICodexProvider(Conversation const &conv, AgentConfig const &config, region_alloc &alloc) -> ProviderResponse
{
    ProviderResponse result;
    result.finishReason = ProviderFinishReason::Error;

    // Get token: config.apiKey first, then env var
    byteview token{};
    if (config.apiKey && config.apiKey[0])
        token = ByteviewFromCStr(config.apiKey);
    else
        TryReadEnvVar("OPENAI_CODEX_TOKEN"_s, token);

    if (token.size == 0)
    {
        result.content =
            AllocAndCopy(alloc, ByteviewFromCStr("OpenAI Codex token not set. "
                                                 "Run coding_agent via scripts/test_ca.sh or set OPENAI_CODEX_TOKEN."));
        return result;
    }

    // Get account ID: config.accountId first, then env var, then extract from JWT
    byteview accountId{};
    if (config.accountId && config.accountId[0])
        accountId = ByteviewFromCStr(config.accountId);
    else
        TryReadEnvVar("OPENAI_CODEX_ACCOUNT_ID"_s, accountId);
    if (accountId.size == 0)
    {
        // Extract from JWT
        accountId = ExtractAccountId(token, alloc);
    }
    if (accountId.size == 0)
    {
        result.content =
            AllocAndCopy(alloc, ByteviewFromCStr("OPENAI_CODEX_ACCOUNT_ID not set and could not extract from token. "
                                                 "Set it to your ChatGPT account ID."));
        return result;
    }

    // Build request body
    byteview requestBody = BuildChatRequestBody(conv, config, alloc);
    if (requestBody.size == 0)
    {
        result.content = AllocAndCopy(alloc, ByteviewFromCStr("Failed to build request body."));
        return result;
    }

    // Build URL
    const char url[] = "https://chatgpt.com/backend-api/codex/responses";
    byteview urlView = ByteviewFromCStr(url);

    // Override URL if config.apiBaseUrl is set
    if (config.apiBaseUrl != nullptr)
        urlView = ByteviewFromCStr(config.apiBaseUrl);

    // Build Authorization header
    constexpr uint64_t kAuthPrefixLen = sizeof("Authorization: Bearer ") - 1;
    constexpr uint64_t kAcctPrefixLen = sizeof("chatgpt-account-id: ") - 1;

    span<char> authBuf = RegionAlloc::AllocArray<char>(alloc, kAuthPrefixLen + token.size + 1);
    MemCpy(authBuf.data, "Authorization: Bearer ", kAuthPrefixLen);
    MemCpy(authBuf.data + kAuthPrefixLen, token.data, token.size);
    authBuf.data[kAuthPrefixLen + token.size] = '\0';

    span<char> acctBuf = RegionAlloc::AllocArray<char>(alloc, kAcctPrefixLen + accountId.size + 1);
    MemCpy(acctBuf.data, "chatgpt-account-id: ", kAcctPrefixLen);
    MemCpy(acctBuf.data + kAcctPrefixLen, accountId.data, accountId.size);
    acctBuf.data[kAcctPrefixLen + accountId.size] = '\0';

    // Build curl args — use -N for no buffering (SSE streaming)
    inline_vec<const char *, 28> curlArgs;
    InlineVec::Clear(curlArgs);
    InlineVec::Append(curlArgs, "curl");
    InlineVec::Append(curlArgs, "-s");
    InlineVec::Append(curlArgs, "-N");
    InlineVec::Append(curlArgs, "-D");
    InlineVec::Append(curlArgs, "/dev/stderr");
    InlineVec::Append(curlArgs, "-o");
    InlineVec::Append(curlArgs, "-");
    InlineVec::Append(curlArgs, "-X");
    InlineVec::Append(curlArgs, "POST");
    InlineVec::Append(curlArgs, "-H");
    InlineVec::Append(curlArgs, authBuf.data);
    InlineVec::Append(curlArgs, "-H");
    InlineVec::Append(curlArgs, acctBuf.data);
    InlineVec::Append(curlArgs, "-H");
    InlineVec::Append(curlArgs, "Content-Type: application/json");
    InlineVec::Append(curlArgs, "-H");
    InlineVec::Append(curlArgs, "originator: pi");
    InlineVec::Append(curlArgs, "-H");
    InlineVec::Append(curlArgs, "OpenAI-Beta: responses=experimental");
    InlineVec::Append(curlArgs, "-H");
    InlineVec::Append(curlArgs, "accept: text/event-stream");
    InlineVec::Append(curlArgs, "--data-binary");
    InlineVec::Append(curlArgs, "@-");
    // Need null-terminated URL
    span<char> urlCStr = RegionAlloc::AllocArray<char>(alloc, urlView.size + 1);
    MemCpy(urlCStr.data, urlView.data, urlView.size);
    urlCStr.data[urlView.size] = '\0';
    InlineVec::Append(curlArgs, urlCStr.data);
    InlineVec::Append(curlArgs, nullptr);

    auto proc = SubprocessRun(span<const char *const>{(const char *const *)curlArgs.begin(), curlArgs.size}, alloc,
                              requestBody, kTimeoutMs);

    if (proc.exit_code != 0)
    {
        uint8_t errBuf[1024];
        uint64_t errLen = StringWriteFmt(span<uint8_t>{errBuf, sizeof(errBuf)},
                                         "Failed to connect to ChatGPT (exit=%d). stderr: %.*s"_s, proc.exit_code,
                                         (int)proc.stderr_data.size, proc.stderr_data.data);
        result.content = AllocAndCopy(alloc, {errBuf, errLen});
        return result;
    }

    // Parse SSE response from stdout
    return ParseSseResponse(proc.stdout_data, alloc);
}

} // namespace nyla
