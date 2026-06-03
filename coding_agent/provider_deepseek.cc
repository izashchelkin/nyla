// ─── DeepSeek + OpenAI provider implementations ───────────────────────────────
// Both use the OpenAI chat completions API format.
// Shared request building and response parsing, differing only in URL and API key source.

#include "coding_agent/provider.h"

#include <cstdio>
#include <cstring>

#include "nyla/commons/fmt.h"
#include "nyla/commons/http_client.h"
#include "nyla/commons/json_parser.h"
#include "nyla/commons/json_value.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/platform.h"

#include "coding_agent/tool_registry.h"
namespace nyla
{

// ─── Constants ──────────────────────────────────────────────────────────────

constexpr uint64_t kMaxBodySize = 512 * 1024; // 512 KB max request body (includes tools)

// ─── Helpers ────────────────────────────────────────────────────────────────

static auto AllocAndCopy(region_alloc &alloc, byteview src) -> byteview
{
    if (src.size == 0)
        return {};
    span<uint8_t> dst = RegionAlloc::AllocArray<uint8_t>(alloc, src.size);
    MemCpy(dst.data, src.data, src.size);
    return {dst.data, src.size};
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
};

// ─── Build JSON request body (OpenAI chat completions format) ───────────────

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

        // Tool call ID (for Tool role)
        if (msg.role == MessageRole::Tool && msg.toolCallId.size > 0)
        {
            bb.WriteRaw("\",\"tool_call_id\":");
            bb.WriteJsonString(msg.toolCallId);
        }

        // Content / tool_calls
        if (msg.role == MessageRole::Assistant && msg.toolCallsJson.size > 0)
        {
            bb.WriteRaw("\",\"tool_calls\":");
            bb.WriteRaw((const char *)msg.toolCallsJson.data, msg.toolCallsJson.size);
        }
        else if (msg.content.size > 0)
        {
            bb.WriteRaw("\",\"content\":");
            bb.WriteJsonString(msg.content);
        }
        else if (msg.role == MessageRole::Assistant && msg.toolCallsJson.size == 0)
        {
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

// ─── Parse response (OpenAI format) ─────────────────────────────────────────

static auto ParseChatResponse(byteview body, region_alloc &alloc) -> ProviderResponse
{
    ProviderResponse result;
    result.finishReason = ProviderFinishReason::Error;

    if (body.size == 0)
        return result;

    constexpr uint64_t kJsonStorageSize = 4096;
    span<json_value> jsonStorage = RegionAlloc::AllocArray<json_value>(alloc, kJsonStorageSize);

    json_parser parser;
    JsonParser::Init(parser, body, jsonStorage);
    json_value *root = JsonParser::ParseNext(parser);
    if (!root || root->tag != json_tag::ObjectBegin)
        return result;

    // Check for error in response body
    {
        byteview errorMsg;
        json_value *errObj;
        if (JsonValue::TryObject(*root, "error"_s, errObj))
        {
            if (JsonValue::TryString(*errObj, "message"_s, errorMsg) && errorMsg.size > 0)
            {
                uint8_t errBuf[512];
                uint64_t errLen =
                    StringWriteFmt(span<uint8_t>{errBuf, sizeof(errBuf)}, "Provider error: %.*s"_s, SV_ARG(errorMsg));
                result.content = AllocAndCopy(alloc, byteview{errBuf, errLen});
                return result;
            }
        }
    }

    // Get choices array
    json_value *choices = JsonValue::Array(*root, "choices"_s);
    if (!choices || JsonValue::GetCount(*choices) == 0)
    {
        const char errMsg[] = "Provider returned no choices.";
        result.content = AllocAndCopy(alloc, {(const uint8_t *)errMsg, (uint32_t)(sizeof(errMsg) - 1)});
        return result;
    }

    json_value &choice = *(JsonValue::GetFront(*choices));
    if (choice.tag != json_tag::ObjectBegin)
        return result;

    // Get finish_reason
    byteview finishReasonStr;
    if (JsonValue::TryString(choice, "finish_reason"_s, finishReasonStr))
    {
        if (ByteviewEq(finishReasonStr, "stop"))
            result.finishReason = ProviderFinishReason::Stop;
        else if (ByteviewEq(finishReasonStr, "tool_calls") || ByteviewEq(finishReasonStr, "function_call"))
            result.finishReason = ProviderFinishReason::ToolCalls;
        else if (ByteviewEq(finishReasonStr, "length"))
            result.finishReason = ProviderFinishReason::Length;
    }

    // Get message object
    json_value *message = JsonValue::Object(choice, "message"_s);
    if (!message)
        return result;

    // Get content (for stop/length)
    if (result.finishReason == ProviderFinishReason::Stop || result.finishReason == ProviderFinishReason::Length)
    {
        byteview content;
        if (JsonValue::TryString(*message, "content"_s, content) && content.size > 0)
            result.content = AllocAndCopy(alloc, content);
    }

    // Get tool_calls
    if (result.finishReason == ProviderFinishReason::ToolCalls)
    {
        json_value *toolCalls = JsonValue::Array(*message, "tool_calls"_s);
        if (toolCalls)
        {
            constexpr uint64_t kToolCallsBufSize = 16384;
            span<uint8_t> tcBuf = RegionAlloc::AllocArray<uint8_t>(alloc, kToolCallsBufSize);
            BufferBuilder tcBB{tcBuf};

            tcBB.WriteRaw("[");
            bool first = true;
            for (auto &tc : *toolCalls)
            {
                if (!first)
                    tcBB.WriteRaw(",");
                first = false;

                byteview tcId = JsonValue::String(tc, "id"_s);
                byteview tcType;
                JsonValue::TryString(tc, "type"_s, tcType);
                byteview fnName;
                byteview fnArgs;
                json_value *fn = JsonValue::Object(tc, "function"_s);
                if (fn)
                {
                    fnName = JsonValue::String(*fn, "name"_s);
                    fnArgs = JsonValue::String(*fn, "arguments"_s);
                }

                tcBB.WriteRaw("{\"id\":");
                tcBB.WriteJsonString(tcId);
                tcBB.WriteRaw(",\"type\":\"function\",\"function\":{\"name\":");
                tcBB.WriteJsonString(fnName);
                tcBB.WriteRaw(",\"arguments\":");
                tcBB.WriteJsonString(fnArgs);
                tcBB.WriteRaw("}}");
            }
            tcBB.WriteRaw("]");

            result.toolCallsJson = {tcBuf.data, tcBB.pos};
        }
    }

    // Extract token usage
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
// ─── Shared provider implementation ─────────────────────────────────────────

static auto CallProvider(byteview defaultUrl, byteview apiKey, Conversation const &conv, AgentConfig const &config,
                         region_alloc &alloc) -> ProviderResponse
{
    ProviderResponse result;
    result.finishReason = ProviderFinishReason::Error;

    // Override URL if config.apiBaseUrl is set
    byteview url = defaultUrl;
    if (config.apiBaseUrl != nullptr)
    {
        uint32_t len = 0;
        while (config.apiBaseUrl[len])
            ++len;
        url = {(const uint8_t *)config.apiBaseUrl, len};
    }

    byteview requestBody = BuildChatRequestBody(conv, config, alloc);
    if (requestBody.size == 0)
    {
        const char errMsg[] = "Failed to build request body.";
        result.content = AllocAndCopy(alloc, {(const uint8_t *)errMsg, (uint32_t)(sizeof(errMsg) - 1)});
        return result;
    }

    if (apiKey.size == 0)
    {
        const char errMsg[] = "API key not set. Set the appropriate env var (e.g. DEEPSEEK_API_KEY, OPENAI_API_KEY).";
        result.content = AllocAndCopy(alloc, {(const uint8_t *)errMsg, (uint32_t)(sizeof(errMsg) - 1)});
        return result;
    }

    http_response httpResp = HttpPostJson(url, requestBody, apiKey, alloc);

    if (httpResp.status_code < 0)
    {
        const char errMsg[] = "Failed to connect to provider. Check network and API base URL.";
        result.content = AllocAndCopy(alloc, {(const uint8_t *)errMsg, (uint32_t)(sizeof(errMsg) - 1)});
        return result;
    }

    if (httpResp.status_code >= 400)
    {
        // For 401/403, give more specific message
        if (httpResp.status_code == 401 || httpResp.status_code == 403)
        {
            const char errMsg[] = "Authentication failed. Check your API key.";
            result.content = AllocAndCopy(alloc, {(const uint8_t *)errMsg, (uint32_t)(sizeof(errMsg) - 1)});
        }
        else
        {
            uint8_t errBuf[256];
            uint64_t errLen = StringWriteFmt(span<uint8_t>{errBuf, sizeof(errBuf)}, "HTTP error %d from provider."_s,
                                             httpResp.status_code);
            result.content = AllocAndCopy(alloc, {errBuf, errLen});
        }
        return result;
    }

    return ParseChatResponse(httpResp.body, alloc);
}

// ─── Public API ─────────────────────────────────────────────────────────────

auto DeepSeekProvider(Conversation const &conv, AgentConfig const &config, region_alloc &alloc) -> ProviderResponse
{
    const char url[] = "https://api.deepseek.com/v1/chat/completions";
    byteview urlView{(const uint8_t *)url, (uint32_t)(sizeof(url) - 1)};

    byteview apiKey{};
    TryReadEnvVar("DEEPSEEK_API_KEY"_s, apiKey);

    return CallProvider(urlView, apiKey, conv, config, alloc);
}

auto OpenAIProvider(Conversation const &conv, AgentConfig const &config, region_alloc &alloc) -> ProviderResponse
{
    const char url[] = "https://api.openai.com/v1/chat/completions";
    byteview urlView{(const uint8_t *)url, (uint32_t)(sizeof(url) - 1)};

    byteview apiKey{};
    TryReadEnvVar("OPENAI_API_KEY"_s, apiKey);

    return CallProvider(urlView, apiKey, conv, config, alloc);
}

} // namespace nyla
