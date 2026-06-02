#pragma once

#include "nyla/commons/span.h"

struct region_alloc;

namespace nyla
{

// Replace `oldText` with `newText` within the structural block at `blockLine`.
auto ReplaceInBlock(byteview filePath, uint32_t blockLine, byteview oldText, byteview newText, region_alloc &alloc)
    -> bool;

// Replace the structural block containing `blockLine` (0-indexed) with content from `newFilePath`.
auto ReplaceBlock(byteview filePath, uint32_t blockLine, byteview newFilePath, region_alloc &alloc) -> bool;

} // namespace nyla
