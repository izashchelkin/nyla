// ─── coding_agent — native C++23 coding agent harness ───
// Headless CLI app built on nyla_commons_headless (no X11 required at runtime).
// Supports ChatGPT Codex and Ollama. Credentials auto-loaded from pi's auth.json.

#include "nyla/commons/entrypoint.h"
#include "nyla/commons/file.h"
#include "nyla/commons/file_utils.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/mempage_pool.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/region_alloc.h"

#include "coding_agent/agent_loop.h"
#include "coding_agent/provider.h"
#include "coding_agent/provider_ollama.h"
#include "coding_agent/provider_openai_codex.h"
#include "coding_agent/tool_registry.h"

namespace nyla
{

// ─── Read pi's auth.json and extract ChatGPT Codex OAuth token ─────────────

// Find a JSON string value: given haystack and a key, finds "key":"<value>"
static auto JsonGetString(byteview haystack, const char *key) -> byteview
{
    uint32_t keyLen = 0;
    while (key[keyLen])
        ++keyLen;

    uint8_t pattern[256];
    pattern[0] = '"';
    for (uint32_t i = 0; i < keyLen && i < 254; ++i)
        pattern[i + 1] = (uint8_t)key[i];
    pattern[keyLen + 1] = '"';

    for (uint64_t i = 0; i + keyLen + 3 < haystack.size; ++i)
    {
        if (!MemEq(haystack.data + i, pattern, keyLen + 2))
            continue;

        uint64_t pos = i + keyLen + 2;
        while (pos < haystack.size &&
               (haystack.data[pos] == ' ' || haystack.data[pos] == ':' || haystack.data[pos] == '\n' ||
                haystack.data[pos] == '\t' || haystack.data[pos] == '\r'))
            ++pos;
        if (pos >= haystack.size || haystack.data[pos] != '"')
            continue;
        ++pos;

        uint64_t valStart = pos;
        while (pos < haystack.size)
        {
            if (haystack.data[pos] == '\\' && pos + 1 < haystack.size)
            {
                pos += 2;
                continue;
            }
            if (haystack.data[pos] == '"')
                break;
            ++pos;
        }
        if (pos >= haystack.size)
            return {};
        return {haystack.data + valStart, pos - valStart};
    }
    return {};
}

static auto LoadPiCredentials(region_alloc &alloc, AgentConfig &config) -> void
{
    const char *home = getenv("HOME");
    if (!home)
        return;

    uint8_t pathBuf[512];
    uint32_t pathLen = 0;
    for (const char *p = home; *p && pathLen < 256; ++p)
        pathBuf[pathLen++] = (uint8_t)*p;
    const char suffix[] = "/.pi/agent/auth.json";
    for (uint32_t i = 0; suffix[i] && pathLen < 500; ++i)
        pathBuf[pathLen++] = (uint8_t)suffix[i];
    pathBuf[pathLen] = 0;

    file_handle fh = FileOpen({pathBuf, pathLen}, FileOpenMode::Read);
    if (!FileValid(fh))
        return;

    byteview jsonText = FileReadFully(alloc, fh);
    FileClose(fh);
    if (jsonText.size == 0)
        return;

    // Find "openai-codex" section
    uint64_t sectionStart = 0;
    {
        const char kSection[] = "\"openai-codex\"";
        uint32_t sLen = (uint32_t)(sizeof(kSection) - 1);
        for (uint64_t i = 0; i + sLen < jsonText.size; ++i)
        {
            if (MemEq(jsonText.data + i, kSection, sLen))
            {
                sectionStart = i + sLen;
                break;
            }
        }
    }
    if (sectionStart == 0)
        return;

    // Extract access token
    byteview accessToken = JsonGetString({jsonText.data + sectionStart, jsonText.size - sectionStart}, "access");
    if (accessToken.size == 0)
        return;

    span<char> tokenBuf = RegionAlloc::AllocArray<char>(alloc, accessToken.size + 1);
    MemCpy(tokenBuf.data, accessToken.data, accessToken.size);
    tokenBuf.data[accessToken.size] = '\0';
    config.apiKey = tokenBuf.data;

    // Account ID is extracted from the JWT by the provider at request time.
    // No need to pre-extract here.
}

void UserMain()
{
    region_alloc alloc = RegionAlloc::Create(MemPagePool::kChunkSize, 0);

    AgentConfig config;
    config.maxToolIterations = 25;
    config.enableToolFailureRepair = true;
    config.helperModel = "gpt-5.4-mini";
    // Wire up the tool registry.
    InitToolRegistry();

    // ─── Auto-load pi credentials ───────────────────────────────────────
    LoadPiCredentials(alloc, config);

    // ─── Model registry ─────────────────────────────────────────────────
    ModelDef availableModels[8];
    uint32_t modelCount = 0;

    // ChatGPT Codex — available if we have a token
    bool hasCodexToken = (config.apiKey && config.apiKey[0]);
    if (!hasCodexToken)
    {
        byteview token{};
        TryReadEnvVar("OPENAI_CODEX_TOKEN"_s, token);
        hasCodexToken = (token.size > 0);
    }

    if (hasCodexToken)
    {
        ModelDef m;
        m.name = "gpt";
        m.displayName = "ChatGPT (gpt-5.4-mini)";
        m.envVars = "";
        m.provider = OpenAICodexProvider;
        m.modelName = "gpt-5.4-mini";
        availableModels[modelCount++] = m;
    }

    // Ollama — always available (no credentials needed)
    {
        ModelDef m;
        m.name = "ollama";
        m.displayName = "Ollama (qwen3:4b)";
        m.envVars = "";
        m.provider = OllamaProvider;
        m.modelName = "qwen3:4b";
        availableModels[modelCount++] = m;
    }

    if (modelCount == 0)
        return;

    // Default to first available
    ProviderFn provider = availableModels[0].provider;
    config.modelName = availableModels[0].modelName;

    RunAgentLoop(alloc, config, provider, ToolRegistryDispatch, span<const ModelDef>{availableModels, modelCount});
}

} // namespace nyla
