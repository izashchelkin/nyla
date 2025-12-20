#pragma once

#include "nyla/rhi/rhi_cmdlist.h"
#include <cstdint>

namespace nyla
{

auto Engine0Init(bool vsync) -> void;
auto Engine0ShouldExit() -> bool;
auto Engine0FrameBegin() -> RhiCmdList;
auto Engine0GetDt() -> float;
auto Engine0GetFps() -> uint32_t;
auto Engine0FrameEnd() -> void;

} // namespace nyla