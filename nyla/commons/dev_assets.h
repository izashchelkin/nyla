#pragma once

#include "nyla/commons/macros.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

namespace DevAssets
{

void API Bootstrap(span<const byteview> roots);

} // namespace DevAssets

} // namespace nyla
