#pragma once

#include "nyla/commons/span.h"

struct region_alloc;

namespace nyla
{

// Move a structural block across files.
// `copyIncludes`: copy #include/import lines from source to destination.
auto MoveBlockInto(byteview srcFilePath, uint32_t srcLine, byteview dstFilePath, uint32_t dstLine, bool copyIncludes,
                   region_alloc &alloc) -> bool;

} // namespace nyla
