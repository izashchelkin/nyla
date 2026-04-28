#pragma once

#include <cstdint>

#include "nyla/commons/macros.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

namespace Tunables
{

void API Bootstrap(byteview persistPath);

void API RegisterFloat(byteview name, float *ptr, float step, float minV, float maxV);
void API RegisterInt(byteview name, int32_t *ptr, int32_t step, int32_t minV, int32_t maxV);

void API ToggleVisible();
auto API IsVisible() -> bool;

void API SelectPrev();
void API SelectNext();
void API Decrement();
void API Increment();

void API Save();

void API CmdFlush(rhi_cmdlist cmd, int32_t originPxX, int32_t originPxY);

} // namespace Tunables

} // namespace nyla
