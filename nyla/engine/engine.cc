#include <chrono>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <thread>

#include "nyla/alloc/region_alloc.h"
#include "nyla/commons/bitenum.h"
#include "nyla/engine/asset_manager.h"
#include "nyla/engine/debug_text_renderer.h"
#include "nyla/engine/engine.h"
#include "nyla/engine/gpu_upload_manager.h"
#include "nyla/engine/input_manager.h"
#include "nyla/engine/renderer.h"
#include "nyla/engine/staging_buffer.h"
#include "nyla/engine/tween_manager.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"

namespace nyla
{

namespace
{

RegionAlloc *m_RootAlloc;
RegionAlloc m_PermanentAlloc;
RegionAlloc m_PerFrameAlloc;

GpuUploadManager m_GpuUploadManager;
AssetManager m_AssetManager;
TweenManager m_TweenManager;
InputManager m_InputManager;

Renderer m_Renderer;
DebugTextRenderer m_DebugTextRenderer;

uint64_t m_TargetFrameDurationUs;

uint64_t m_LastFrameStart;
uint32_t m_DtUsAccum;
uint32_t m_FramesCounted;
uint32_t m_Fps;
bool m_ShouldExit;

} // namespace

auto Engine::GetPermanentAlloc() -> RegionAlloc &
{
    return m_PermanentAlloc;
}

auto Engine::GetPerFrameAlloc() -> RegionAlloc &
{
    return m_PerFrameAlloc;
}

void Engine::Init(const EngineInitDesc &desc)
{
    m_LastFrameStart = Platform::GetMonotonicTimeMicros();

    m_RootAlloc = desc.rootAlloc;
    m_PermanentAlloc = m_RootAlloc->PushSubAlloc(16_MiB);
    m_PerFrameAlloc = m_RootAlloc->PushSubAlloc(16_MiB);

    uint32_t maxFps = 144;
    if (desc.maxFps > 0)
        maxFps = desc.maxFps;

    m_TargetFrameDurationUs = static_cast<uint64_t>(1'000'000.0 / maxFps);

    RhiFlags flags = None<RhiFlags>();
    if (desc.vsync)
        flags |= RhiFlags::VSync;

    Rhi::Init(RhiInitDesc{
        .flags = flags,
    });

    m_GpuUploadManager.Init();
    m_AssetManager.Init();

    m_Renderer.Init();
    m_DebugTextRenderer.Init();
}

auto Engine::ShouldExit() -> bool
{
    return m_ShouldExit;
}

auto Engine::FrameBegin() -> EngineFrameBeginResult
{
    RhiCmdList cmd = Rhi::FrameBegin();

    const uint64_t frameStart = Platform::GetMonotonicTimeMicros();

    const uint64_t dtUs = frameStart - m_LastFrameStart;
    m_DtUsAccum += dtUs;
    ++m_FramesCounted;

    const float dt = static_cast<float>(dtUs) * 1e-6f;
    m_LastFrameStart = frameStart;

    if (m_DtUsAccum >= 500'000ull)
    {
        const double seconds = static_cast<double>(m_DtUsAccum) / 1'000'000.0;
        const double fpsF64 = m_FramesCounted / seconds;

        m_Fps = std::lround(fpsF64);
        m_DtUsAccum = 0;
        m_FramesCounted = 0;
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

auto Engine::FrameEnd() -> void
{
    Rhi::FrameEnd();

    uint64_t frameEnd = Platform::GetMonotonicTimeMicros();
    uint64_t frameDurationUs = frameEnd - m_LastFrameStart;

    if (m_TargetFrameDurationUs > frameDurationUs)
    {
        using namespace std::chrono_literals;
        std::this_thread::sleep_for((m_TargetFrameDurationUs - frameDurationUs) * 1us);
    }
}

//

Engine g_Engine{};

} // namespace nyla