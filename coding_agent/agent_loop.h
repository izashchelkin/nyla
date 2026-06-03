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

    // Approximate context-window management. Token estimates use bytes / 4 and
    // provider-reported totalTokens when available. Set contextWindowTokens to 0 to disable.
    uint32_t contextWindowTokens = 32768;
    uint32_t contextWarnTokens = 28000;
    uint32_t contextCompactTokens = 30000;
    uint32_t contextKeepRecentMessages = 32;
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

// ─── Stub tool dispatch (replaced by real registry in 3.3/3.4) ─────────────

auto StubDispatchTool(byteview toolCallId, byteview toolName, byteview toolArguments, region_alloc &alloc)
    -> ToolResult;

// ─── Agent loop ─────────────────────────────────────────────────────────────

void RunAgentLoop(region_alloc &alloc, AgentConfig const &config, ProviderFn provider, DispatchToolFn dispatchTool);
} // namespace nyla
