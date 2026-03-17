#pragma once

#include "nyla/alloc/region_alloc.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include <cstdint>

namespace nyla
{

struct EngineInitDesc
{
    uint32_t maxFps;
    bool vsync;

    RegionAlloc *rootAlloc;
};

struct EngineFrameBeginResult
{
    RhiCmdList cmd;
    float dt;
    uint32_t fps;
};

class Engine
{
  public:
    static void Init(const EngineInitDesc &);
    static auto ShouldExit() -> bool;

    static auto FrameBegin() -> EngineFrameBeginResult;
    static auto FrameEnd() -> void;

    static auto GetPermanentAlloc() -> RegionAlloc &;

    static auto GetPerFrameAlloc() -> RegionAlloc &;

  private:
};

} // namespace nyla