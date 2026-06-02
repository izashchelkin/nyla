#pragma once

#include "nyla/commons/region_alloc.h"
#include "nyla/commons/span.h"

namespace nyla
{

struct InlineResult
{
    bool ok;
    const char *error;
};

auto InlineFunctionCall(byteview filePath, uint32_t callSiteLine, region_alloc &alloc) -> InlineResult;

} // namespace nyla
