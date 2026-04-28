#pragma once

#include <cstdint>

#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

namespace DevLog
{

void API Bootstrap();
void API Push(byteview line);
auto API Snapshot(region_alloc &alloc, uint32_t maxLines) -> span<byteview>;

} // namespace DevLog

} // namespace nyla
