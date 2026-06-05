#pragma once

#include "nyla/commons/region_alloc.h"
#include "nyla/commons/span.h"

#include "coding_agent/agent_loop.h"

namespace nyla
{

// ─── JSON string unescape helper ───────────────────────────────────────────
// The JSON parser stores strings with raw escape sequences (\n, \t, \r, \\, \").
// This helper converts them to actual characters. Writes to dst (must be at
// least src.size bytes). Returns the unescaped byteview.
INLINE auto UnescapeJsonString(byteview src, span<uint8_t> dst) -> byteview
{
    uint32_t out = 0;
    for (uint32_t i = 0; i < src.size; ++i)
    {
        if (src.data[i] == '\\' && i + 1 < src.size)
        {
            ++i; // skip backslash
            uint8_t next = src.data[i];
            switch (next)
            {
            case 'n':
                dst.data[out++] = '\n';
                break;
            case 'r':
                dst.data[out++] = '\r';
                break;
            case 't':
                dst.data[out++] = '\t';
                break;
            case '\\':
                dst.data[out++] = '\\';
                break;
            case '"':
                dst.data[out++] = '"';
                break;
            default:
                dst.data[out++] = next;
                break;
            }
        }
        else
        {
            dst.data[out++] = src.data[i];
        }
    }
    return {dst.data, out};
}

// ─── Provider declarations ─────────────────────────────────────────────────
// All providers follow the ProviderFn signature: (Conversation, AgentConfig, region_alloc) -> ProviderResponse

// DeepSeek — talks to api.deepseek.com/v1/chat/completions
// API key from DEEPSEEK_API_KEY env var.
auto DeepSeekProvider(Conversation const &conv, AgentConfig const &config, region_alloc &alloc) -> ProviderResponse;

// OpenAI -- talks to api.openai.com/v1/chat/completions
// API key from OPENAI_API_KEY env var.
auto OpenAIProvider(Conversation const &conv, AgentConfig const &config, region_alloc &alloc) -> ProviderResponse;

// OpenAI Codex (ChatGPT subscription) -- talks to chatgpt.com/backend-api/codex/responses.
// OAuth access token from OPENAI_CODEX_TOKEN env var.
// Account ID from OPENAI_CODEX_ACCOUNT_ID env var (auto-extracted from JWT if not set).
// Defined in provider_openai_codex.cc.
auto OpenAICodexProvider(Conversation const &conv, AgentConfig const &config, region_alloc &alloc) -> ProviderResponse;

// Ollama -- talks to a local Ollama instance at http://localhost:11434/api/chat.
// No API key needed. Defined in provider_ollama.cc.
auto OllamaProvider(Conversation const &conv, AgentConfig const &config, region_alloc &alloc) -> ProviderResponse;

// Stub provider — for testing without a model. Defined in provider_ollama.cc.
auto StubProvider(Conversation const &conv, AgentConfig const &config, region_alloc &alloc) -> ProviderResponse;

} // namespace nyla
