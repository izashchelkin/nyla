#pragma once

#include "nyla/commons/span.h"

struct region_alloc;

namespace nyla
{

// Insert content before a structural block.
// `blockLine` is 0-indexed. `contentFilePath` contains the code to insert.
auto InsertBeforeBlock(byteview filePath, uint32_t blockLine, byteview contentFilePath, region_alloc &alloc) -> bool;

// Insert content after a structural block.
auto InsertAfterBlock(byteview filePath, uint32_t blockLine, byteview contentFilePath, region_alloc &alloc) -> bool;

} // namespace nyla
