#pragma once

#include "nyla/commons/span.h"

struct region_alloc;

namespace nyla
{

// ─── Block operations ───────────────────────────────────────────────────────

// Move the structural block containing `blockLine` to after `destLine`.
// Both are 0-indexed. The block is re-indented to match destination context.
auto MoveBlock(byteview filePath, uint32_t blockLine, uint32_t destLine, region_alloc &alloc) -> bool;

// Move a structural block across files.
// `copyIncludes`: copy #include/import lines from source to destination.
auto MoveBlockInto(byteview srcFilePath, uint32_t srcLine, byteview dstFilePath, uint32_t dstLine, bool copyIncludes,
                   region_alloc &alloc) -> bool;

// Delete the structural block containing `blockLine` (0-indexed).
auto DeleteBlock(byteview filePath, uint32_t blockLine, region_alloc &alloc) -> bool;

// Replace `oldText` with `newText` within the structural block at `blockLine`.
auto ReplaceInBlock(byteview filePath, uint32_t blockLine, byteview oldText, byteview newText, region_alloc &alloc)
    -> bool;

// Safe global edit with tree-sitter validation.
// `oldFile`/`newFile` are paths to temp files containing the old/new text.
// `force` skips validation. `dryRun` reports match without writing.
auto EditSafe(byteview filePath, byteview oldFile, byteview newFile, region_alloc &alloc, bool force = false,
              bool dryRun = false, bool diff = false) -> bool;

// Move a block to a new file with #pragma once, namespace wrapping, and #include wiring.
auto BlockExtract(byteview srcFilePath, uint32_t srcLine, byteview dstFilePath, region_alloc &alloc) -> bool;

// Replace the structural block containing `blockLine` (0-indexed) with content from `newFilePath`.
auto ReplaceBlock(byteview filePath, uint32_t blockLine, byteview newFilePath, region_alloc &alloc) -> bool;

// Re-indent every structural block via brace-counting + whitespace cleanup.
auto TidyFile(byteview filePath, region_alloc &alloc) -> bool;

} // namespace nyla
