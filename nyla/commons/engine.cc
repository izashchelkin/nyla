#include "nyla/commons/engine.h"

#include <cstdint>

#include "nyla/commons/dir_watcher.h"
#include "nyla/commons/input_manager.h"
#include "nyla/commons/intrin.h"
#include "nyla/commons/keyboard.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/profiler.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/renderdoc.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/time.h"
#include "nyla/commons/tunables.h"

namespace nyla
{

namespace
{

struct engine_state
{
    uint64_t targetFrameDurationUs;
    uint64_t lastFrameStartUs;
    uint32_t dtUsAccum;
    uint32_t framesCounted;
    uint32_t fps;
    bool shouldExit;
};
engine_state *g_engine;

} // namespace

namespace Engine
{

void API Bootstrap(region_alloc &alloc, const engine_init_desc &desc)
{
    g_engine = &RegionAlloc::Alloc<engine_state>(RegionAlloc::g_BootstrapAlloc);

    const uint32_t maxFps = desc.maxFps ? desc.maxFps : 144;
    g_engine->targetFrameDurationUs = 1'000'000ull / maxFps;
    g_engine->lastFrameStartUs = GetMonotonicTimeMicros();

    rhi_flags flags{};
    if (desc.vsync)
        flags |= rhi_flags::VSync;

    WinOpen();
    Rhi::Bootstrap(alloc, rhi_init_desc{.flags = flags});
    InputManager::Bootstrap();
    DirWatcher::Bootstrap(alloc);
}

auto API FrameBegin(region_alloc &alloc) -> engine_frame
{
    RegionAlloc::Reset(alloc);

    DirWatcher::Tick();

    Profiler::FrameBegin();

    rhi_cmdlist cmd = Rhi::FrameBegin(alloc);

    const uint64_t frameStart = GetMonotonicTimeMicros();
    const uint64_t dtUs = frameStart - g_engine->lastFrameStartUs;
    const float dt = static_cast<float>(dtUs) * 1e-6f;
    g_engine->lastFrameStartUs = frameStart;

    g_engine->dtUsAccum += static_cast<uint32_t>(dtUs);
    ++g_engine->framesCounted;
    if (g_engine->dtUsAccum >= 500'000u)
    {
        const double seconds = static_cast<double>(g_engine->dtUsAccum) / 1'000'000.0;
        const double fpsF64 = static_cast<double>(g_engine->framesCounted) / seconds;
        g_engine->fps = static_cast<uint32_t>(LRound(fpsF64));
        g_engine->dtUsAccum = 0;
        g_engine->framesCounted = 0;
    }

    for (; !ShouldExit();)
    {
        PlatformEvent event{};
        if (!WinPollEvent(event))
            break;

        switch (event.type)
        {
        case PlatformEventType::KeyDown:
            InputManager::HandlePressed(input_interface_type::Keyboard, uint32_t(event.key), frameStart);
#if !defined(NDEBUG)
            switch (event.key)
            {
            case KeyPhysical::F1:
                Tunables::ToggleVisible();
                break;
            case KeyPhysical::F2:
                Tunables::SelectPrev();
                break;
            case KeyPhysical::F3:
                Tunables::SelectNext();
                break;
            case KeyPhysical::F4:
                Tunables::Decrement();
                break;
            case KeyPhysical::F5:
                Tunables::Increment();
                break;
            case KeyPhysical::F6:
                Tunables::Save();
                break;
            case KeyPhysical::F7:
                Profiler::ToggleVisible();
                break;
            case KeyPhysical::F11:
                RenderDocTriggerCapture();
                break;
            default:
                break;
            }
#else
            if (event.key == KeyPhysical::F11)
                RenderDocTriggerCapture();
#endif
            break;
        case PlatformEventType::KeyUp:
            InputManager::HandleReleased(input_interface_type::Keyboard, uint32_t(event.key), frameStart);
            break;
        case PlatformEventType::MousePress:
            InputManager::HandlePressed(input_interface_type::Mouse, event.mouse.code, frameStart);
            break;
        case PlatformEventType::MouseRelease:
            InputManager::HandleReleased(input_interface_type::Mouse, event.mouse.code, frameStart);
            break;
        case PlatformEventType::WinResize:
            Rhi::TriggerSwapchainRecreate();
            break;
        case PlatformEventType::Quit:
            g_engine->shouldExit = true;
            break;
        case PlatformEventType::Repaint:
        case PlatformEventType::None:
            break;
        default:
            TRAP();
            break;
        }
    }

    return engine_frame{
        .cmd = cmd,
        .dt = dt,
        .dtUs = dtUs,
        .frameStartUs = frameStart,
        .fps = g_engine->fps,
    };
}

void API FrameEnd(region_alloc &alloc)
{
    Profiler::FrameEnd();

    Rhi::FrameEnd(alloc);

    const uint64_t frameEndUs = GetMonotonicTimeMicros();
    const uint64_t frameDurationUs = frameEndUs - g_engine->lastFrameStartUs;

    if (g_engine->targetFrameDurationUs > frameDurationUs)
    {
        const uint64_t sleepForMillis = (g_engine->targetFrameDurationUs - frameDurationUs) / 1000;
        if (sleepForMillis)
            Sleep(sleepForMillis);
    }
}

auto API ShouldExit() -> bool
{
    return g_engine->shouldExit;
}

} // namespace Engine

} // namespace nyla