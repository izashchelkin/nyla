#pragma once

#include <cstdint>

#include "nyla/commons/macros.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

namespace Profiler
{

void API Bootstrap();

void API FrameBegin();
void API FrameEnd();

void API BeginScope(byteview name);
void API EndScope();

void API ToggleVisible();
auto API IsVisible() -> bool;

void API CmdFlush(rhi_cmdlist cmd, int32_t originPxX, int32_t originPxY, uint32_t fps);

} // namespace Profiler

} // namespace nyla