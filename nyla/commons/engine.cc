#include <concepts>
#include <cstdint>

#include "nyla/commons/asset_manager.h"
#include "nyla/commons/bitenum.h"
#include "nyla/commons/debug_text_renderer.h"
#include "nyla/commons/engine.h"
#include "nyla/commons/gpu_upload_manager.h"
#include "nyla/commons/input_manager.h"
#include "nyla/commons/intrin.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/renderer.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/staging_buffer.h"
#include "nyla/commons/tween_manager.h"

namespace nyla
{

namespace
{

struct engine
{
    region_alloc perFrameAlloc;

    uint64_t targetFrameDurationUs;
    uint64_t lastFrameStartUs;
    uint32_t dtUsAccum;
    uint32_t framesCounted;
    uint32_t fps;
    bool shouldExit;
};
engine *g_Engine;

} // namespace

namespace Engine
{

void API Init(const EngineInitDesc &desc)
{
    g_Engine = &RegionAlloc::Alloc<engine>(RegionAlloc::g_BootstrapAlloc);
    g_Engine->perFrameAlloc = RegionAlloc::Create(64_MiB, 0);

    g_Engine->lastFrameStartUs = Platform::GetMonotonicTimeMicros();

    uint32_t maxFps = 144;
    if (desc.maxFps > 0)
        maxFps = desc.maxFps;

    g_Engine->targetFrameDurationUs = static_cast<uint64_t>(1'000'000.0 / maxFps);

    rhi_flags flags = None<rhi_flags>();
    if (desc.vsync)
        flags |= rhi_flags::VSync;

    Rhi::Init(rhi_init_desc{
        .flags = flags,
    });

    GpuUploadManager::Init();
    AssetManager::Init();
    Renderer::Init();
    DebugTextRenderer::Init();
}

auto API FrameBegin() -> EngineFrameBeginResult
{
    rhi_cmdlist cmd = Rhi::FrameBegin();

    const uint64_t frameStart = Platform::GetMonotonicTimeMicros();

    const uint64_t dtUs = frameStart - g_Engine->lastFrameStartUs;
    g_Engine->dtUsAccum += dtUs;
    ++g_Engine->framesCounted;

    const float dt = static_cast<float>(dtUs) * 1e-6f;
    g_Engine->lastFrameStartUs = frameStart;

    if (g_Engine->dtUsAccum >= 500'000ull)
    {
        const double seconds = static_cast<double>(g_Engine->dtUsAccum) / 1'000'000.0;
        const double fpsF64 = g_Engine->framesCounted / seconds;

        g_Engine->fps = LRound(fpsF64);
        g_Engine->dtUsAccum = 0;
        g_Engine->framesCounted = 0;
    }

    for (;;)
    {
        PlatformEvent event{};
        if (!Platform::WinPollEvent(event))
            break;

        switch (event.type)
        {
        case PlatformEventType::KeyDown: {
            m_InputManager.HandlePressed(1, uint32_t(event.key), frameStart);
            break;
        }
        case PlatformEventType::KeyUp: {
            m_InputManager.HandleReleased(1, uint32_t(event.key), frameStart);
            break;
        }

        case PlatformEventType::MousePress: {
            m_InputManager.HandlePressed(2, event.mouse.code, frameStart);
            break;
        }
        case PlatformEventType::MouseRelease: {
            m_InputManager.HandleReleased(2, event.mouse.code, frameStart);
            break;
        }

        case PlatformEventType::WinResize: {
            Rhi::TriggerSwapchainRecreate();
            break;
        }

        case PlatformEventType::Quit: {
            std::exit(0);
        }

        case PlatformEventType::Repaint:
        case PlatformEventType::None:
            break;
        }
    }

    m_InputManager.Update();
    m_TweenManager.Update(dt);

    m_GpuUploadManager.FrameBegin();
    m_AssetManager.Upload(cmd);

    return {
        .cmd = cmd,
        .dt = dt,
        .fps = m_Fps,
    };
}

} // namespace Engine

auto API FrameEnd() -> void
{
    Rhi::FrameEnd();

    uint64_t frameEndUs = Platform::GetMonotonicTimeMicros();
    uint64_t frameDurationUs = frameEndUs - g_Engine->lastFrameStartUs;

    uint64_t sleepForMillis = (g_Engine->targetFrameDurationUs - frameDurationUs) / 1000;
    if (sleepForMillis)
        Platform::Sleep(sleepForMillis);
}

auto API GetPerFrameAlloc() -> region_alloc &
{
    return g_Engine->perFrameAlloc;
}

auto API ShouldExit() -> bool
{
    return g_Engine->shouldExit;
}

} // namespace nyla