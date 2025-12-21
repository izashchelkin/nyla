#pragma once

#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include <cstdint>

namespace nyla
{

struct Engine0Desc
{
    uint32_t maxFps;
    PlatformWindow window;
    bool vsync;
};

auto Engine0Init(Engine0Desc) -> void;
auto Engine0ShouldExit() -> bool;
auto Engine0FrameBegin() -> RhiCmdList;
auto Engine0GetDt() -> float;
auto Engine0GetFps() -> uint32_t;
auto Engine0FrameEnd() -> void;

} // namespace nyla