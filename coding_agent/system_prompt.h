#pragma once

#include "nyla/commons/region_alloc.h"
#include "nyla/commons/span.h"

namespace nyla
{

// Build the system prompt for the coding agent.
// 1. Tries to load ~/.config/coding_agent/prompt.md (user override)
// 2. Falls back to an embedded default prompt
// 3. Appends tool descriptions from the tool registry
// 4. Appends project context (cwd, file tree summary)
// Returns a region-allocated byteview suitable for use as Message.content.
auto BuildSystemPrompt(region_alloc &alloc) -> byteview;

} // namespace nyla
