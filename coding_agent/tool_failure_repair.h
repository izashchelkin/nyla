#pragma once

#include "coding_agent/agent_loop.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/span.h"

namespace nyla
{

// ─── Failure classification ────────────────────────────────────────────────

enum class FailureLayer : uint8_t
{
    // Deterministic fix (JSON truncation, unescape) — no LLM needed.
    Mechanical,

    // Needs semantic understanding — send to helper LLM for repair or summary.
    Semantic,

    // The main model SHOULD see this error (test failures, build errors, etc.).
    PassThrough,
};

// Classify a tool failure to determine which layer should handle it.
// Called after dispatchTool returns isError=true.
auto ClassifyToolFailure(byteview toolName, byteview errorContent) -> FailureLayer;

// ─── Layer 0: Deterministic mechanical repair ──────────────────────────────

// Try to fix tool arguments deterministically (JSON truncation, known typos).
// Returns fixed args on success. Returns empty byteview if unfixable.
auto TryMechanicalRepair(byteview toolName, byteview originalArgs, byteview errorContent, region_alloc &alloc)
    -> byteview;

// ─── Layer 1: LLM repair or summarize ──────────────────────────────────────

// Ask a helper model (same provider as main, clean context) to either fix the
// arguments or summarize the failure. Uses config.helperModel as the model name.
// On success: returns repaired ToolResult (isError=false, content starts with
// "[REPAIR_RETRY]" followed by fixed args JSON).
// On failure: returns summarized error (isError=true).
auto TryLLMRepair(byteview toolName, byteview originalArgs, byteview errorContent, AgentConfig const &config,
                  ProviderFn provider, region_alloc &alloc) -> ToolResult;

} // namespace nyla
