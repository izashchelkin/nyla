#pragma once

#include "nyla/commons/region_alloc.h"
#include "nyla/commons/span.h"

#include "coding_agent/agent_loop.h"

namespace nyla
{

// ─── Provider declarations ─────────────────────────────────────────────────
// All providers follow the ProviderFn signature: (Conversation, AgentConfig, region_alloc) -> ProviderResponse

// DeepSeek — talks to api.deepseek.com/v1/chat/completions
// API key from DEEPSEEK_API_KEY env var.
auto DeepSeekProvider(Conversation const &conv, AgentConfig const &config, region_alloc &alloc) -> ProviderResponse;

// OpenAI — talks to api.openai.com/v1/chat/completions
// API key from OPENAI_API_KEY env var.
auto OpenAIProvider(Conversation const &conv, AgentConfig const &config, region_alloc &alloc) -> ProviderResponse;

// Ollama — talks to a local Ollama instance at http://localhost:11434/api/chat.
// No API key needed. Defined in provider_ollama.cc.
auto OllamaProvider(Conversation const &conv, AgentConfig const &config, region_alloc &alloc) -> ProviderResponse;

// Stub provider — for testing without a model. Defined in provider_ollama.cc.
auto StubProvider(Conversation const &conv, AgentConfig const &config, region_alloc &alloc) -> ProviderResponse;

} // namespace nyla
