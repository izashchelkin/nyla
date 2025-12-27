#pragma once

#include "nyla/engine/asset_manager.h"
#include "nyla/engine/input_manager.h"
#include "nyla/engine/staging_buffer.h"
#include "nyla/engine/tween_manager.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include <cstdint>

namespace nyla
{

class Engine;

#define NYLA_ENGINE_EXTERN_GLOBALS(X)                                                                                  \
    X(Engine *g_Engine)                                                                                                \
    X(AssetManager *g_AssetManager)                                                                                    \
    X(GpuStagingBuffer *g_StagingBuffer)                                                                               \
    X(TweenManager *g_TweenManager)                                                                                    \
    X(InputManager *g_InputManager)

#define X(x) extern x;
NYLA_ENGINE_EXTERN_GLOBALS(X)
#undef X

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

struct EngineState;

class Engine
{
  public:
    void Init(const EngineInitDesc &);
    auto ShouldExit() -> bool;

    auto FrameBegin() -> EngineFrameBeginResult;
    auto FrameEnd() -> void;

  private:
    EngineState *m_State{};
};

} // namespace nyla