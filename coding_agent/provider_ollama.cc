// ─── Ollama provider implementation ──────────────────────────────────────────
// Talks to Ollama's /api/chat endpoint (OpenAI-compatible format).
// Builds JSON request body manually, parses response with JsonParser.

#include "coding_agent/provider_ollama.h"

#include <cstdio>
#include <cstring>

#include "nyla/commons/fmt.h"
#include "nyla/commons/http_client.h"
#include "nyla/commons/json_parser.h"
#include "nyla/commons/json_value.h"
#include "nyla/commons/mem.h"

#include "coding_agent/tool_registry.h"
namespace nyla
{

// ─── Constants ──────────────────────────────────────────────────────────────

constexpr uint64_t kMaxBodySize = 256 * 1024; // 256 KB max request body

// ─── Helpers ────────────────────────────────────────────────────────────────

// Allocate and copy a byteview into a region allocator. Returns region-allocated copy.
static auto AllocAndCopy(region_alloc &alloc, byteview src) -> byteview
{
    if (src.size == 0)
        return {};
    span<uint8_t> dst = RegionAlloc::AllocArray<uint8_t>(alloc, src.size);
    MemCpy(dst.data, src.data, src.size);
    return {dst.data, src.size};
}

// ─── JSON escaping ──────────────────────────────────────────────────────────
// Writes the JSON-escaped version of src into dst. Returns total bytes that would be
// written (even if dst is too small — caller must ensure enough space).

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
// Mini builder for appending to a fixed buffer with position tracking.

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

    auto WriteJsonString(byteview content) -> bool
    {
        if (pos >= buf.size)
            return false;
        buf.data[pos] = '"';
        pos++;

        span<uint8_t> escapeBuf{buf.data + pos, buf.size - pos - 1}; // room for closing "
        uint64_t escaped = JsonEscapeString(escapeBuf, content);
        pos += escaped;

        if (pos >= buf.size)
            return false;
        buf.data[pos] = '"';
        pos++;
        return true;
    }
};

// ─── Build JSON request body ────────────────────────────────────────────────

static auto BuildChatRequestBody(Conversation const &conv, AgentConfig const &config, region_alloc &alloc) -> byteview
{
    span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(alloc, kMaxBodySize);
    BufferBuilder bb{buf};

    // ── Open object + "model" field ──
    bb.WriteRaw("{\"model\":\"");
    bb.WriteRaw(config.modelName);
    bb.WriteRaw("\",\"messages\":[");

    // ── Messages array ──
    for (uint64_t i = 0; i < conv.size; ++i)
    {
        Message const &msg = conv.data[i];

        if (i > 0)
            bb.WriteRaw(",");

        bb.WriteRaw("{\"role\":\"");

        const char *roleStr = "user";
        switch (msg.role)
        {
        case MessageRole::System:
            roleStr = "system";
            break;
        case MessageRole::User:
            roleStr = "user";
            break;
        case MessageRole::Assistant:
            roleStr = "assistant";
            break;
        case MessageRole::Tool:
            roleStr = "tool";
            break;
        }
        bb.WriteRaw(roleStr);

        // Tool call ID + content (for Tool role)
        if (msg.role == MessageRole::Tool && msg.toolCallId.size > 0)
        {
            bb.WriteRaw("\",\"tool_call_id\":");
            bb.WriteJsonString(msg.toolCallId);
            if (msg.content.size > 0)
            {
                bb.WriteRaw(",\"content\":");
                bb.WriteJsonString(msg.content);
            }
        }
        // Content (for non-tool-call / non-tool messages)
        else if (msg.role == MessageRole::Assistant && msg.toolCallsJson.size > 0)
        {
            // Ollama doesn't need tool_calls in history and its format is
            // incompatible (arguments as object vs our stored JSON string).
            // Just write null content to mark the assistant turn.
            bb.WriteRaw("\",\"content\":null");
        }
        else if (msg.content.size > 0)
        {
            bb.WriteRaw("\",\"content\":");
            bb.WriteJsonString(msg.content);
        }
        else if (msg.role == MessageRole::Assistant && msg.content.size == 0 && msg.toolCallsJson.size == 0)
        {
            // Empty assistant message with no tool calls — null content
            bb.WriteRaw("\",\"content\":null");
        }
        else
        {
            bb.WriteRaw("\",\"content\":\"\"");
        }

        bb.WriteRaw("}");
    }

    // ── Close messages, start tools ──
    bb.WriteRaw("],\"tools\":[");

    // ── Tools array (from registry) ──
    span<const ToolDef> tools = GetToolDefs();
    for (uint64_t i = 0; i < tools.size; ++i)
    {
        ToolDef const &tool = tools.data[i];
        if (i > 0)
            bb.WriteRaw(",");
        bb.WriteRaw("{\"type\":\"function\",\"function\":{\"name\":\"");
        bb.WriteRaw(tool.name);
        bb.WriteRaw("\",\"description\":");
        bb.WriteJsonString({(const uint8_t *)tool.description, (uint32_t)strlen(tool.description)});
        bb.WriteRaw(",\"parameters\":");
        bb.WriteRaw(tool.parametersJson);
        bb.WriteRaw("}}");
    }

    // ── Close tools, add max_tokens/temperature/stream ──
    bb.WriteRaw("]");
    if (config.maxTokens > 0)
    {
        uint8_t numBuf[32];
        uint32_t numLen = StringWriteFmt(span<uint8_t>{numBuf, sizeof(numBuf)}, "%u"_s, config.maxTokens);
        bb.WriteRaw(",\"max_tokens\":");
        bb.WriteRaw((const char *)numBuf, numLen);
    }
    {
        char numBuf[32];
        int numLen = snprintf(numBuf, sizeof(numBuf), "%.1f", (double)config.temperature);
        bb.WriteRaw(",\"temperature\":");
        bb.WriteRaw(numBuf, (uint32_t)numLen);
    }
    bb.WriteRaw(",\"stream\":false}");
    return {buf.data, bb.pos};
}

// ─── Parse response ─────────────────────────────────────────────────────────

static auto ParseChatResponse(byteview body, region_alloc &alloc) -> ProviderResponse
{
    ProviderResponse result;
    result.finishReason = ProviderFinishReason::Error;

    if (body.size == 0)
        return result;

    // Allocate JSON value storage — generous size
    constexpr uint64_t kJsonStorageSize = 4096;
    span<json_value> jsonStorage = RegionAlloc::AllocArray<json_value>(alloc, kJsonStorageSize);

    json_parser parser;
    JsonParser::Init(parser, body, jsonStorage);
    json_value *root = JsonParser::ParseNext(parser);
    if (!root || root->tag != json_tag::ObjectBegin)
        return result;

    // Check for error in response
    {
        byteview errorMsg;
        if (JsonValue::TryString(*root, "error"_s, errorMsg))
        {
            span<uint8_t> unesc = RegionAlloc::AllocArray<uint8_t>(alloc, errorMsg.size);
            byteview unescaped = UnescapeJsonString(errorMsg, unesc);
            uint8_t errBuf[256];
            uint64_t errLen =
                StringWriteFmt(span<uint8_t>{errBuf, sizeof(errBuf)}, "Provider error: %.*s"_s, SV_ARG(unescaped));
            result.content = AllocAndCopy(alloc, byteview{errBuf, errLen});
            return result;
        }
    }

    // Get finish reason from "done_reason"
    byteview doneReason;
    if (JsonValue::TryString(*root, "done_reason"_s, doneReason))
    {
        if (ByteviewEq(doneReason, "stop"))
            result.finishReason = ProviderFinishReason::Stop;
        else if (ByteviewEq(doneReason, "tool_calls") || ByteviewEq(doneReason, "function_call"))
            result.finishReason = ProviderFinishReason::ToolCalls;
        else if (ByteviewEq(doneReason, "length"))
            result.finishReason = ProviderFinishReason::Length;
    }
    else
    {
        // Some models omit done_reason on stop — check "done" field
        bool done = false;
        if (JsonValue::TryBool(*root, "done"_s, done) && done)
            result.finishReason = ProviderFinishReason::Stop;
    }

    // Get message object
    json_value *message = JsonValue::Any(*root, "message"_s);
    if (!message)
        return result;

    // Get tool_calls (check regardless of done_reason — some Ollama models
    // return done_reason:"stop" even when tool_calls are present)
    {
        json_value *toolCalls;
        if (JsonValue::TryArray(*message, "tool_calls"_s, toolCalls) && toolCalls)
        {
            result.finishReason = ProviderFinishReason::ToolCalls;
        }
    }

    // Get content
    if (result.finishReason == ProviderFinishReason::Stop || result.finishReason == ProviderFinishReason::Length)
    {
        byteview content;
        if (JsonValue::TryString(*message, "content"_s, content) && content.size > 0)
        {
            span<uint8_t> unesc = RegionAlloc::AllocArray<uint8_t>(alloc, content.size);
            byteview unescaped = UnescapeJsonString(content, unesc);
            result.content = AllocAndCopy(alloc, unescaped);
        }
    }

    // Serialize tool_calls
    if (result.finishReason == ProviderFinishReason::ToolCalls)
    {
        json_value *toolCalls;
        JsonValue::TryArray(*message, "tool_calls"_s, toolCalls);
        if (toolCalls)
        {
            // Serialize the tool_calls array back to JSON for storage in the conversation
            constexpr uint64_t kToolCallsBufSize = 16384;
            span<uint8_t> tcBuf = RegionAlloc::AllocArray<uint8_t>(alloc, kToolCallsBufSize);
            BufferBuilder tcBB{tcBuf};

            tcBB.WriteRaw("[");
            bool first = true;
            uint32_t idx = 0;
            for (auto &tc : *toolCalls)
            {
                if (!first)
                    tcBB.WriteRaw(",");
                first = false;

                byteview tcId{};
                JsonValue::TryString(tc, "id"_s, tcId);
                byteview tcType{};
                JsonValue::TryString(tc, "type"_s, tcType);
                byteview fnName{};
                byteview fnArgs{};
                json_value *fn = JsonValue::Object(tc, "function"_s);
                if (fn)
                {
                    JsonValue::TryString(*fn, "name"_s, fnName);
                    // arguments can be a JSON string (OpenAI format) or a raw
                    // JSON object (some Ollama models). Handle both.
                    if (!JsonValue::TryString(*fn, "arguments"_s, fnArgs))
                    {
                        json_value *argsObj;
                        if (JsonValue::TryObject(*fn, "arguments"_s, argsObj))
                        {
                            // Serialize the arguments object back to a JSON string
                            constexpr uint64_t kArgsBuf = 4096;
                            span<uint8_t> ab = RegionAlloc::AllocArray<uint8_t>(alloc, kArgsBuf);
                            uint64_t ap = 0;
                            ab[ap++] = '{';
                            bool first = true;
                            // Object children are stored as alternating key,value,... -- iterate in pairs
                            json_value *children = JsonValue::GetFront(*argsObj);
                            uint32_t pairCount = JsonValue::GetCount(*argsObj);
                            for (uint32_t ci = 0; ci < pairCount; ++ci)
                            {
                                json_value *k = children + ci * 2;
                                json_value *v = children + ci * 2 + 1;
                                if (k && v && k->tag == json_tag::String)
                                {
                                    if (!first && ap + 1 < kArgsBuf)
                                        ab[ap++] = ',';
                                    first = false;
                                    if (ap + k->val.valStr.size + 3 < kArgsBuf)
                                    {
                                        ab[ap++] = '"';
                                        MemCpy(ab.data + ap, k->val.valStr.data, k->val.valStr.size);
                                        ap += k->val.valStr.size;
                                        ab[ap++] = '"';
                                        ab[ap++] = ':';
                                    }
                                    if (v->tag == json_tag::String)
                                    {
                                        if (ap + v->val.valStr.size + 3 < kArgsBuf)
                                        {
                                            ab[ap++] = '"';
                                            MemCpy(ab.data + ap, v->val.valStr.data, v->val.valStr.size);
                                            ap += v->val.valStr.size;
                                            ab[ap++] = '"';
                                        }
                                    }
                                    else if (v->tag == json_tag::Integer)
                                    {
                                        uint8_t nb[32];
                                        uint32_t nl = StringWriteFmt(span<uint8_t>{nb, sizeof(nb)}, "%lld"_s,
                                                                     (long long)v->val.valInt);
                                        if (ap + nl < kArgsBuf)
                                        {
                                            MemCpy(ab.data + ap, nb, nl);
                                            ap += nl;
                                        }
                                    }
                                    else if (v->tag == json_tag::Bool)
                                    {
                                        const char *bv = v->val.valBool ? "true" : "false";
                                        uint32_t bl = v->val.valBool ? 4 : 5;
                                        if (ap + bl < kArgsBuf)
                                        {
                                            MemCpy(ab.data + ap, bv, bl);
                                            ap += bl;
                                        }
                                    }
                                }
                            }
                            if (ap < kArgsBuf)
                                ab[ap++] = '}';
                            fnArgs = {ab.data, ap};
                        }
                    }
                }
                tcBB.WriteRaw("{\"id\":");
                if (tcId.size > 0)
                    tcBB.WriteJsonString(tcId);
                else
                {
                    // Ollama may not provide an id -- generate a synthetic one
                    uint8_t idBuf[32];
                    uint32_t idLen = StringWriteFmt(span<uint8_t>{idBuf, sizeof(idBuf)}, "call_%u"_s, idx);
                    tcBB.WriteRaw("\"");
                    tcBB.WriteRaw((const char *)idBuf, idLen);
                    tcBB.WriteRaw("\"");
                }
                tcBB.WriteRaw(",\"type\":\"function\",\"function\":{\"name\":");
                tcBB.WriteJsonString(fnName);
                tcBB.WriteRaw(",\"arguments\":");
                tcBB.WriteJsonString(fnArgs);
                tcBB.WriteRaw("}}");
                ++idx;
            }
            tcBB.WriteRaw("]");

            result.toolCallsJson = {tcBuf.data, tcBB.pos};
        }
    }

    // Extract token usage (Ollama format -- some models provide this)
    {
        json_value *usage;
        if (JsonValue::TryObject(*root, "usage"_s, usage))
        {
            uint32_t total = 0;
            if (JsonValue::TryDWord(*usage, "total_tokens"_s, total))
                result.totalTokens = total;
        }
    }
    return result;
}

// ─── Public API ─────────────────────────────────────────────────────────────

auto OllamaProvider(Conversation const &conv, AgentConfig const &config, region_alloc &alloc) -> ProviderResponse
{
    ProviderResponse result;
    result.finishReason = ProviderFinishReason::Error;

    byteview requestBody = BuildChatRequestBody(conv, config, alloc);
    if (requestBody.size == 0)
    {
        const char errMsg[] = "Failed to build request body.";
        result.content = AllocAndCopy(alloc, byteview{(const uint8_t *)errMsg, (uint32_t)(sizeof(errMsg) - 1)});
        return result;
    }

    // Ollama runs locally on port 11434, no TLS, no auth
    // Use config.apiBaseUrl if set, otherwise default
    byteview urlView;
    if (config.apiBaseUrl != nullptr)
    {
        uint32_t len = 0;
        while (config.apiBaseUrl[len])
            ++len;
        urlView = {(const uint8_t *)config.apiBaseUrl, len};
    }
    else
    {
        const char url[] = "http://localhost:11434/api/chat";
        urlView = {(const uint8_t *)url, (uint32_t)(sizeof(url) - 1)};
    }
    byteview emptyKey{};
    http_response httpResp = HttpPostJson(urlView, requestBody, emptyKey, alloc);
    if (httpResp.status_code < 0)
    {
        const char errMsg[] = "Failed to connect to Ollama. Is the server running? (ollama serve)";
        result.content = AllocAndCopy(alloc, byteview{(const uint8_t *)errMsg, (uint32_t)(sizeof(errMsg) - 1)});
        return result;
    }

    if (httpResp.status_code >= 400)
    {
        uint8_t errBuf[256];
        uint64_t errLen =
            StringWriteFmt(span<uint8_t>{errBuf, sizeof(errBuf)}, "HTTP error %d from Ollama."_s, httpResp.status_code);
        result.content = AllocAndCopy(alloc, byteview{errBuf, errLen});
        return result;
    }

    return ParseChatResponse(httpResp.body, alloc);
}

// ─── Stub provider ──────────────────────────────────────────────────────────

auto StubProvider(Conversation const & /*conv*/, AgentConfig const & /*config*/, region_alloc &alloc)
    -> ProviderResponse
{
    ProviderResponse resp;
    resp.finishReason = ProviderFinishReason::Stop;
    const char msg[] = "Provider not yet implemented (Phase 3.6). Type 'exit' to quit.\n";
    resp.content = AllocAndCopy(alloc, byteview{(const uint8_t *)msg, (uint32_t)(sizeof(msg) - 1)});
    return resp;
}

} // namespace nyla
