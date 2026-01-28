#pragma once

#include "nyla/commons/memory/region_alloc.h"
#include "nyla/engine/asset_manager.h"
#include "nyla/engine/debug_text_renderer.h"
#include "nyla/engine/gpu_upload_manager.h"
#include "nyla/engine/renderer.h"
#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include <concepts>
#include <cstdint>

namespace nyla
{

class Engine;

struct EngineInitDesc
{
    uint32_t maxFps;
    bool vsync;

    RegionAlloc &rootAlloc;
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

    auto GetAssetManager() -> AssetManager &
    {
        return m_AssetManager;
    }

    auto GetUploadManager() -> GpuUploadManager &
    {
        return m_GpuUploadManager;
    }

    auto GetPerFrameAlloc() -> RegionAlloc &
    {
        return m_PerFrameAlloc;
    }

    auto GetRenderer2D() -> Renderer &
    {
        return m_Renderer2d;
    }

    auto GetDebugTextRenderer() -> DebugTextRenderer &
    {
        return m_DebugTextRenderer;
    }

  private:
    RegionAlloc *m_RootAlloc;
    RegionAlloc m_PermanentAlloc;
    RegionAlloc m_PerFrameAlloc;

    GpuUploadManager m_GpuUploadManager;
    AssetManager m_AssetManager;

    Renderer m_Renderer2d;
    DebugTextRenderer m_DebugTextRenderer;

    uint64_t m_TargetFrameDurationUs;

    uint64_t m_LastFrameStart;
    uint32_t m_DtUsAccum;
    uint32_t m_FramesCounted;
    uint32_t m_Fps;
    bool m_ShouldExit;
};
extern Engine g_Engine;

} // namespace nyla