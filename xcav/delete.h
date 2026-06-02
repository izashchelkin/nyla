#pragma once

#include "nyla/commons/span.h"

struct region_alloc;

namespace nyla
{

// Delete the structural block containing `blockLine` (0-indexed).
auto DeleteBlock(byteview filePath, uint32_t blockLine, region_alloc &alloc) -> bool;

} // namespace nyla
