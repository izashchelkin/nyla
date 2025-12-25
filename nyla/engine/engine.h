#pragma once

#include "nyla/engine/asset_manager.h"
#include "nyla/engine/staging_buffer.h"
#include "nyla/engine/tween_manager.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include <cstdint>

namespace nyla
{

#define NYLA_ENGINE_EXTERN_GLOBALS(X)                                                                                  \
    X(AssetManager *assetManager)                                                                                      \
    X(GpuStagingBuffer *stagingBuffer)                                                                                 \
    X(TweenManager *tweenManager)

#define X(x) extern x;
NYLA_ENGINE_EXTERN_GLOBALS(X)
#undef X

struct EngineInitDesc
{
    uint32_t maxFps;
    PlatformWindow window;
    bool vsync;
};

void EngineInit(const EngineInitDesc &);
auto EngineShouldExit() -> bool;
auto EngineFrameBegin() -> RhiCmdList;
auto EngineGetDt() -> float;
auto EngineGetFps() -> uint32_t;
auto EngineFrameEnd() -> void;

} // namespace nyla