// ─── coding_agent — native C++23 coding agent harness ───
// Headless CLI app built on nyla_commons_headless (no X11 required at runtime).
// Talks to DeepSeek/OpenAI and uses xcav CmdXxx functions for structural code editing.

#include "nyla/commons/entrypoint.h"
#include "nyla/commons/mempage_pool.h"
#include "nyla/commons/region_alloc.h"

#include "coding_agent/agent_loop.h"
#include "coding_agent/provider.h"
#include "coding_agent/tool_registry.h"

namespace nyla
{

void UserMain()
{
    region_alloc alloc = RegionAlloc::Create(MemPagePool::kChunkSize, 0);

    AgentConfig config;
    config.modelName = "qwen3:4b";
    config.maxToolIterations = 25;

    // Wire up the tool registry and provider.
    InitToolRegistry();

    // Choose provider:
    // - OllamaProvider: local Ollama instance at localhost:11434 (no API key needed)
    // - DeepSeekProvider: api.deepseek.com (needs DEEPSEEK_API_KEY env var)
    // - OpenAIProvider: api.openai.com (needs OPENAI_API_KEY env var)
    RunAgentLoop(alloc, config, OllamaProvider, ToolRegistryDispatch);
}

} // namespace nyla
