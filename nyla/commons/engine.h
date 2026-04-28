#pragma once

#include <cstdint>

#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/rhi.h"

namespace nyla
{

struct engine_init_desc
{
    uint32_t maxFps;
    bool vsync;
};

struct engine_frame
{
    rhi_cmdlist cmd;
    float dt;
    uint64_t dtUs;
    uint64_t frameStartUs;
    uint32_t fps;
};

namespace Engine
{

void API Bootstrap(region_alloc &alloc, const engine_init_desc &desc);
auto API FrameBegin(region_alloc &alloc) -> engine_frame;
void API FrameEnd(region_alloc &alloc);
auto API ShouldExit() -> bool;

} // namespace Engine

} // namespace nyla
