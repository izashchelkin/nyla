#pragma once

#include "nyla/commons/span.h"

struct region_alloc;

namespace nyla
{

// Move the structural block containing `blockLine` to after `destLine`.
// Both are 0-indexed. The block is re-indented to match destination context.
auto MoveBlock(byteview filePath, uint32_t blockLine, uint32_t destLine, region_alloc &alloc) -> bool;

} // namespace nyla
