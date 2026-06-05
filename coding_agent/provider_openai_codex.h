#pragma once

#include "coding_agent/agent_loop.h"

namespace nyla
{

// OpenAI Codex (ChatGPT subscription) provider.
// Uses the Codex Responses API at chatgpt.com/backend-api/codex/responses.
// Auth: OAuth access token + account ID (extracted from JWT).
// Reads OPENAI_CODEX_TOKEN and OPENAI_CODEX_ACCOUNT_ID env vars.
auto OpenAICodexProvider(Conversation const &conv, AgentConfig const &config, region_alloc &alloc) -> ProviderResponse;

} // namespace nyla
