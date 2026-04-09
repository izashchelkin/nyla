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
    RhiCmdList cmd;
    float dt;
    uint32_t fps;
};

namespace Engine
{

void NYLA_API Init(const EngineInitDesc &);
auto NYLA_API ShouldExit() -> bool;

auto NYLA_API FrameBegin() -> EngineFrameBeginResult;
auto NYLA_API FrameEnd() -> void;

auto NYLA_API GetPerFrameAlloc() -> region_alloc &;

} // namespace Engine

} // namespace nyla