#pragma once

#include "coding_agent/agent_loop.h"
#include "nyla/commons/inline_vec.h"

namespace nyla
{

// ─── Tool handler type ─────────────────────────────────────────────────────

// Function pointer for a tool handler.
// argsJson: raw JSON string of the tool arguments (e.g. {"file":"foo.cc","line":42})
// alloc: region allocator for result strings
using ToolHandler = auto (*)(byteview argsJson, region_alloc &alloc) -> ToolResult;

// ─── Tool definition ───────────────────────────────────────────────────────

struct ToolDef
{
    const char *name;           // e.g. "xcav_blocks"
    const char *description;    // human-readable, for the LLM system prompt
    const char *parametersJson; // JSON Schema for function calling parameters
    ToolHandler handler;        // handler function
};

// ─── Registry API ──────────────────────────────────────────────────────────

// Initialize the global tool registry with all available tools.
// Must be called once before ToolRegistryDispatch or GetToolDefs.
void InitToolRegistry();

// Dispatch a tool call by name. Returns error result for unknown tools.
auto ToolRegistryDispatch(byteview toolCallId, byteview toolName, byteview toolArguments, region_alloc &alloc)
    -> ToolResult;

// Get the list of registered tool definitions (for JSON serialization to providers).
auto GetToolDefs() -> span<const ToolDef>;

} // namespace nyla
