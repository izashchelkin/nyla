#pragma once

#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include <cstdint>

namespace nyla
{

class Engine;

struct EngineInitDesc
{
    uint32_t maxFps;
    PlatformWindow window;
    bool vsync;
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
    void Init(const EngineInitDesc &);
    auto ShouldExit() -> bool;

    auto FrameBegin() -> EngineFrameBeginResult;
    auto FrameEnd() -> void;

  private:
    class Impl;
    Impl *m_Impl{};
};
extern Engine *g_Engine;

} // namespace nyla