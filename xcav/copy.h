#pragma once

#include "nyla/commons/inline_vec.h"
#include "nyla/commons/span.h"

struct region_alloc;

namespace nyla
{

struct CopyResult
{
    bool ok;
    const char *error;
    inline_vec<uint32_t, 16> returnLines; // 1-indexed, block-relative
};

auto CopyBlock(byteview srcFilePath, uint32_t srcLine, byteview dstFilePath, uint32_t dstLine, bool showReturns,
               bool copyIncludes, region_alloc &alloc) -> CopyResult;

} // namespace nyla
