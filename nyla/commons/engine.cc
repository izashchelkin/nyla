#include "nyla/commons/engine.h"

#include <cstdint>

#include "nyla/commons/dir_watcher.h"
#include "nyla/commons/inline_vec.h"
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

    // Pointer state carried across frames; edges + delta reset each FrameBegin.
    int32_t pointerX;
    int32_t pointerY;
    uint32_t pointerButtons;
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
    DirWatcher::Bootstrap();
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

    constexpr uint64_t kTextBufCap = 256;
    inline_vec<uint8_t, kTextBufCap> textBuf{};

    const int32_t pointerStartX = g_engine->pointerX;
    const int32_t pointerStartY = g_engine->pointerY;
    uint32_t pointerPressEdges = 0;
    uint32_t pointerReleaseEdges = 0;

    for (; !ShouldExit();)
    {
        PlatformEvent event{};
        if (!WinPollEvent(event))
            break;

        if (event.textLen > 0)
        {
            // InlineVec::Append asserts size+n < Capacity (strict), so leave one slot unused.
            uint64_t limit = kTextBufCap - 1;
            uint64_t room = textBuf.size < limit ? limit - textBuf.size : 0;
            uint64_t take = event.textLen < room ? event.textLen : room;
            if (take > 0)
                InlineVec::Append(textBuf, byteview{event.textBytes, take});
        }

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
            g_engine->pointerX = event.mouse.x;
            g_engine->pointerY = event.mouse.y;
            if (event.mouse.code >= 1 && event.mouse.code <= 32)
            {
                uint32_t bit = 1u << (event.mouse.code - 1);
                pointerPressEdges |= bit;
                g_engine->pointerButtons |= bit;
            }
            break;
        case PlatformEventType::MouseRelease:
            InputManager::HandleReleased(input_interface_type::Mouse, event.mouse.code, frameStart);
            g_engine->pointerX = event.mouse.x;
            g_engine->pointerY = event.mouse.y;
            if (event.mouse.code >= 1 && event.mouse.code <= 32)
            {
                uint32_t bit = 1u << (event.mouse.code - 1);
                pointerReleaseEdges |= bit;
                g_engine->pointerButtons &= ~bit;
            }
            break;
        case PlatformEventType::MouseMove:
            g_engine->pointerX = event.mouse.x;
            g_engine->pointerY = event.mouse.y;
            break;
        case PlatformEventType::WinResize:
            Rhi::TriggerSwapchainRecreate();
            break;
        case PlatformEventType::Quit:
            g_engine->shouldExit = true;
            break;
        case PlatformEventType::TextInput:
        case PlatformEventType::Repaint:
        case PlatformEventType::None:
            break;
        default:
            TRAP();
            break;
        }
    }

    byteview textChars{};
    if (textBuf.size > 0)
        textChars = RegionAlloc::CopyByteView(alloc, byteview{textBuf.data.data, textBuf.size});

    return engine_frame{
        .cmd = cmd,
        .dt = dt,
        .dtUs = dtUs,
        .frameStartUs = frameStart,
        .fps = g_engine->fps,
        .textChars = textChars,
        .pointerX = g_engine->pointerX,
        .pointerY = g_engine->pointerY,
        .pointerDx = g_engine->pointerX - pointerStartX,
        .pointerDy = g_engine->pointerY - pointerStartY,
        .pointerButtons = g_engine->pointerButtons,
        .pointerPress = pointerPressEdges,
        .pointerRelease = pointerReleaseEdges,
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

void API RequestExit()
{
    g_engine->shouldExit = true;
}

} // namespace Engine

} // namespace nyla
