#pragma once

#include "nyla/commons/inline_vec.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/span.h"

namespace nyla
{

// ─── Message types ──────────────────────────────────────────────────────────

enum class MessageRole : uint8_t
{
    System,
    User,
    Assistant,
    Tool,
};

struct Message
{
    MessageRole role;
    byteview content;       // region-allocated text
    byteview toolCallId;    // for Tool role: the tool_call id being responded to
    byteview toolCallsJson; // for Assistant role: raw JSON array of tool_calls (empty if none)
};

using Conversation = inline_vec<Message, 256>;

// ─── Configuration ──────────────────────────────────────────────────────────

struct AgentConfig
{
    const char *modelName = "deepseek-chat";
    uint32_t maxToolIterations = 25;
    uint32_t maxTokens = 4096;        // max output tokens -- 0 = provider default
    float temperature = 0.7f;         // 0.0 = deterministic, 1.0 = creative
    const char *apiBaseUrl = nullptr; // override API base URL (null = provider default)

    // Provider credentials (auto-loaded from pi's auth.json at startup).
    // Providers check these first, then fall back to env vars.
    const char *apiKey = nullptr;    // OAuth token / API key
    const char *accountId = nullptr; // ChatGPT account ID (for Codex provider)

    // Approximate context-window management. Token estimates use bytes / 4 and
    // provider-reported totalTokens when available. Set contextWindowTokens to 0 to disable.
    uint32_t contextWindowTokens = 32768;
    uint32_t contextWarnTokens = 28000;
    uint32_t contextCompactTokens = 30000;
    uint32_t contextKeepRecentMessages = 32;

    // Tool failure repair -- uses a cheap local model to fix or summarize
    // failed tool calls before the main model sees them.
    bool enableToolFailureRepair = true;      // gate for Layer 0 + Layer 1
    const char *helperModel = "gpt-5.4-mini"; // model for repair/summarize (Layer 1)
};
// ─── Provider response ──────────────────────────────────────────────────────

enum class ProviderFinishReason : uint8_t
{
    Stop,
    ToolCalls,
    Length,
    Error,
};

struct ProviderResponse
{
    ProviderFinishReason finishReason = ProviderFinishReason::Stop;
    byteview content;         // region-allocated text (for Stop)
    byteview thinkingContent; // region-allocated reasoning/thinking text (for Stop, displayed before content)
    byteview toolCallsJson;   // region-allocated JSON array of tool_calls (for ToolCalls)
    uint32_t totalTokens = 0; // usage.total_tokens from API response (0 = unknown)
};
// ─── Tool dispatch ──────────────────────────────────────────────────────────

struct ToolResult
{
    byteview content;
    bool isError = false;
};

// Called by the agent loop to execute a tool. Returns the result as a string.
// Stub for now — real dispatch comes in 3.3/3.4.
using DispatchToolFn = auto (*)(byteview toolCallId, byteview toolName, byteview toolArguments, region_alloc &alloc)
    -> ToolResult;

// ─── Provider function type (defined in provider_ollama.h) ──────────────────

using ProviderFn = auto (*)(Conversation const &conv, AgentConfig const &config, region_alloc &alloc)
    -> ProviderResponse;

// ─── Model definition ─────────────────────────────────────────────────────

struct ModelDef
{
    const char *name;        // e.g. "gpt-5.4-mini" or "qwen3:4b"
    const char *displayName; // e.g. "ChatGPT (gpt-5.4-mini)"
    const char *envVars;     // space-separated env vars needed (empty = none)
    ProviderFn provider;     // provider function
    const char *modelName;   // model name to pass to the provider
};

// ─── Stub tool dispatch (replaced by real registry in 3.3/3.4) ─────────────

auto StubDispatchTool(byteview toolCallId, byteview toolName, byteview toolArguments, region_alloc &alloc)
    -> ToolResult;

// ─── Agent loop ─────────────────────────────────────────────────────────────

// Runs the REPL loop. models[] is the list of available models; models[0] is the default.
// Pass a ProviderFn* so the loop can switch it when the user runs /model.
void RunAgentLoop(region_alloc &alloc, AgentConfig &config, ProviderFn &provider, DispatchToolFn dispatchTool,
                  span<const ModelDef> models);
} // namespace nyla
