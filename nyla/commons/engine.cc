#if 0

#include <concepts>
#include <cstdint>

#include "nyla/commons/bitenum.h"
#include "nyla/commons/debug_text_renderer.h"
#include "nyla/commons/engine.h"
#include "nyla/commons/gpu_upload.h"
#include "nyla/commons/input_manager.h"
#include "nyla/commons/intrin.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/renderer.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/time.h"
#include "nyla/commons/tween_manager.h"

namespace nyla
{

namespace
{

struct engine_state
{
    region_alloc perFrameAlloc;

    uint64_t targetFrameDurationUs;
    uint64_t lastFrameStartUs;
    uint32_t dtUsAccum;
    uint32_t framesCounted;
    uint32_t fps;
    bool shouldExit;
};
engine_state *engine;

} // namespace

namespace Engine
{

void API Init(const EngineInitDesc &desc)
{
    engine = &RegionAlloc::Alloc<engine_state>(RegionAlloc::g_BootstrapAlloc);
    engine->perFrameAlloc = RegionAlloc::Create(64_MiB, 0);

    engine->lastFrameStartUs = GetMonotonicTimeMicros();

    uint32_t maxFps = 144;
    if (desc.maxFps > 0)
        maxFps = desc.maxFps;

    engine->targetFrameDurationUs = static_cast<uint64_t>(1'000'000.0 / maxFps);

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

    const uint64_t frameStart = GetMonotonicTimeMicros();

    const uint64_t dtUs = frameStart - engine->lastFrameStartUs;
    engine->dtUsAccum += dtUs;
    ++engine->framesCounted;

    const float dt = static_cast<float>(dtUs) * 1e-6f;
    engine->lastFrameStartUs = frameStart;

    if (engine->dtUsAccum >= 500'000ull)
    {
        const double seconds = static_cast<double>(engine->dtUsAccum) / 1'000'000.0;
        const double fpsF64 = engine->framesCounted / seconds;

        engine->fps = LRound(fpsF64);
        engine->dtUsAccum = 0;
        engine->framesCounted = 0;
    }

    for (;;)
    {
        PlatformEvent event{};
        if (!WinPollEvent(event))
            break;

        switch (event.type)
        {

        case PlatformEventType::KeyDown: {
            InputManager::HandlePressed(input_interface_type::Keyboard, uint32_t(event.key), frameStart);
            break;
        }
        case PlatformEventType::KeyUp: {
            InputManager::HandleReleased(input_interface_type::Keyboard, uint32_t(event.key), frameStart);
            break;
        }

        case PlatformEventType::MousePress: {
            InputManager::HandlePressed(input_interface_type::Mouse, event.mouse.code, frameStart);
            break;
        }
        case PlatformEventType::MouseRelease: {
            InputManager::HandleReleased(input_interface_type::Mouse, event.mouse.code, frameStart);
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
    uint64_t frameDurationUs = frameEndUs - engine->lastFrameStartUs;

    uint64_t sleepForMillis = (engine->targetFrameDurationUs - frameDurationUs) / 1000;
    if (sleepForMillis)
        Platform::Sleep(sleepForMillis);
}

auto API GetPerFrameAlloc() -> region_alloc &
{
    return engine->perFrameAlloc;
}

auto API ShouldExit() -> bool
{
    return engine->shouldExit;
}

} // namespace nyla

#endif