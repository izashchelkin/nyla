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

    // Modifier mask carried across frames; bits = KeyMod::*.
    uint8_t modMask;

    bool isWindowResized;
};
engine_state *engine;

} // namespace

namespace Engine
{

void API Bootstrap(region_alloc &alloc, const engine_init_desc &desc)
{
    engine = &RegionAlloc::Alloc<engine_state>(RegionAlloc::g_BootstrapAlloc);

    const uint32_t maxFps = desc.maxFps ? desc.maxFps : 144;
    engine->targetFrameDurationUs = 1'000'000ull / maxFps;
    engine->lastFrameStartUs = GetMonotonicTimeMicros();

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
    const uint64_t dtUs = frameStart - engine->lastFrameStartUs;
    const float dt = static_cast<float>(dtUs) * 1e-6f;
    engine->lastFrameStartUs = frameStart;

    engine->dtUsAccum += static_cast<uint32_t>(dtUs);
    ++engine->framesCounted;
    if (engine->dtUsAccum >= 500'000u)
    {
        const double seconds = static_cast<double>(engine->dtUsAccum) / 1'000'000.0;
        const double fpsF64 = static_cast<double>(engine->framesCounted) / seconds;
        engine->fps = static_cast<uint32_t>(LRound(fpsF64));
        engine->dtUsAccum = 0;
        engine->framesCounted = 0;
    }

    constexpr uint64_t kTextBufCap = 256;
    inline_vec<uint8_t, kTextBufCap> textBuf{};

    constexpr uint64_t kKeyDownCap = 64;
    inline_vec<key_event, kKeyDownCap> keyDownBuf{};

    const int32_t pointerStartX = engine->pointerX;
    const int32_t pointerStartY = engine->pointerY;
    uint32_t pointerPressEdges = 0;
    uint32_t pointerReleaseEdges = 0;

    auto modBit = [](KeyPhysical k) -> uint8_t {
        switch (k)
        {
        case KeyPhysical::LeftShift:
        case KeyPhysical::RightShift:
            return KeyMod::Shift;
        case KeyPhysical::LeftCtrl:
        case KeyPhysical::RightCtrl:
            return KeyMod::Ctrl;
        case KeyPhysical::LeftAlt:
        case KeyPhysical::RightAlt:
            return KeyMod::Alt;
        default:
            return 0;
        }
    };

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
            if (uint8_t bit = modBit(event.key))
                engine->modMask |= bit;
            if (keyDownBuf.size + 1 < kKeyDownCap)
                InlineVec::Append(keyDownBuf, key_event{.key = event.key, .mods = engine->modMask});
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
            if (uint8_t bit = modBit(event.key))
                engine->modMask &= ~bit;
            break;
        case PlatformEventType::MousePress:
            InputManager::HandlePressed(input_interface_type::Mouse, event.mouse.code, frameStart);
            engine->pointerX = event.mouse.x;
            engine->pointerY = event.mouse.y;
            if (event.mouse.code >= 1 && event.mouse.code <= 32)
            {
                uint32_t bit = 1u << (event.mouse.code - 1);
                pointerPressEdges |= bit;
                engine->pointerButtons |= bit;
            }
            break;
        case PlatformEventType::MouseRelease:
            InputManager::HandleReleased(input_interface_type::Mouse, event.mouse.code, frameStart);
            engine->pointerX = event.mouse.x;
            engine->pointerY = event.mouse.y;
            if (event.mouse.code >= 1 && event.mouse.code <= 32)
            {
                uint32_t bit = 1u << (event.mouse.code - 1);
                pointerReleaseEdges |= bit;
                engine->pointerButtons &= ~bit;
            }
            break;
        case PlatformEventType::MouseMove:
            engine->pointerX = event.mouse.x;
            engine->pointerY = event.mouse.y;
            break;
        case PlatformEventType::WinResize: {
            Rhi::TriggerSwapchainRecreate();
            engine->isWindowResized = true;
            break;
        }
        case PlatformEventType::Quit:
            engine->shouldExit = true;
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

    span<const key_event> keyDown{};
    if (keyDownBuf.size > 0)
    {
        auto *dst = (key_event *)RegionAlloc::Alloc(alloc, sizeof(key_event) * keyDownBuf.size, alignof(key_event));
        for (uint64_t i = 0; i < keyDownBuf.size; ++i)
            dst[i] = keyDownBuf.data.data[i];
        keyDown = span<const key_event>{dst, keyDownBuf.size};
    }

    return engine_frame{
        .cmd = cmd,
        .dt = dt,
        .dtUs = dtUs,
        .frameStartUs = frameStart,
        .fps = engine->fps,
        .textChars = textChars,
        .keyDown = keyDown,
        .pointerX = engine->pointerX,
        .pointerY = engine->pointerY,
        .pointerDx = engine->pointerX - pointerStartX,
        .pointerDy = engine->pointerY - pointerStartY,
        .pointerButtons = engine->pointerButtons,
        .pointerPress = pointerPressEdges,
        .pointerRelease = pointerReleaseEdges,
    };
}

void API FrameEnd(region_alloc &alloc)
{
    Profiler::FrameEnd();

    Rhi::FrameEnd(alloc);

    const uint64_t frameEndUs = GetMonotonicTimeMicros();
    const uint64_t frameDurationUs = frameEndUs - engine->lastFrameStartUs;

    if (engine->targetFrameDurationUs > frameDurationUs)
    {
        const uint64_t sleepForMillis = (engine->targetFrameDurationUs - frameDurationUs) / 1000;
        if (sleepForMillis)
            Sleep(sleepForMillis);
    }
}

auto API IsWindowResized() -> bool
{
    if (engine->isWindowResized)
    {
        engine->isWindowResized = false;
        return true;
    }
    return false;
}

auto API ShouldExit() -> bool
{
    return engine->shouldExit;
}

void API RequestExit()
{
    engine->shouldExit = true;
}

} // namespace Engine

} // namespace nyla