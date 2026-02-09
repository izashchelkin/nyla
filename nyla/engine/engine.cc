#include "nyla/engine/engine.h"
#include "nyla/commons/bitenum.h"
#include "nyla/engine/asset_manager.h"
#include "nyla/engine/input_manager.h"
#include "nyla/engine/staging_buffer.h"
#include "nyla/engine/tween_manager.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <thread>

namespace nyla
{

void Engine::Init(const EngineInitDesc &desc)
{
    m_LastFrameStart = g_Platform.GetMonotonicTimeMicros();

    m_RootAlloc = &desc.rootAlloc;
    m_PermanentAlloc = m_RootAlloc->PushSubAlloc(16_MiB);
    m_PerFrameAlloc = m_RootAlloc->PushSubAlloc(16_MiB);

    uint32_t maxFps = 144;
    if (desc.maxFps > 0)
        maxFps = desc.maxFps;

    m_TargetFrameDurationUs = static_cast<uint64_t>(1'000'000.0 / maxFps);

    RhiFlags flags = None<RhiFlags>();
    if (desc.vsync)
        flags |= RhiFlags::VSync;

    g_Rhi.Init(RhiInitDesc{
        .flags = flags,
    });

    m_GpuUploadManager.Init();
    m_AssetManager.Init();

    m_Renderer2d.Init();
    m_DebugTextRenderer.Init();
}

auto Engine::ShouldExit() -> bool
{
    return m_ShouldExit;
}

auto Engine::FrameBegin() -> EngineFrameBeginResult
{
    RhiCmdList cmd = g_Rhi.FrameBegin();

    const uint64_t frameStart = g_Platform.GetMonotonicTimeMicros();

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
        if (!g_Platform.PollEvent(event))
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
            g_Rhi.TriggerSwapchainRecreate();
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
    g_Rhi.FrameEnd();

    uint64_t frameEnd = g_Platform.GetMonotonicTimeMicros();
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