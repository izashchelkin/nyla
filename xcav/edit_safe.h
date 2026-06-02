#pragma once

#include "nyla/commons/span.h"

struct region_alloc;

namespace nyla
{

// Safe global edit with tree-sitter validation.
// `oldFile`/`newFile` are paths to temp files containing the old/new text.
// `force` skips validation. `dryRun` reports match without writing.
auto EditSafe(byteview filePath, byteview oldFile, byteview newFile, region_alloc &alloc, bool dryRun = false) -> bool;
} // namespace nyla
