#pragma once

#include <cstdint>

#include "nyla/commons/region_alloc.h"
#include "nyla/commons/rhi.h"

namespace nyla
{

struct EngineInitDesc
{
    uint32_t maxFps;
    bool vsync;
};

struct EngineFrameBeginResult
{
    rhi_cmdlist cmd;
    float dt;
    uint32_t fps;
};

namespace Engine
{

void API Init(const EngineInitDesc &);
auto API ShouldExit() -> bool;

auto API FrameBegin() -> EngineFrameBeginResult;
auto API FrameEnd() -> void;

auto API GetPerFrameAlloc() -> region_alloc &;

} // namespace Engine

} // namespace nyla