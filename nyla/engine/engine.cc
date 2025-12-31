#include "nyla/engine/engine.h"
#include "nyla/commons/bitenum.h"
#include "nyla/commons/os/clock.h"
#include "nyla/engine/asset_manager.h"
#include "nyla/engine/frame_arena.h"
#include "nyla/engine/input_manager.h"
#include "nyla/engine/staging_buffer.h"
#include "nyla/engine/tween_manager.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <thread>

namespace nyla
{

class Engine::Impl
{
  public:
    void Init(const EngineInitDesc &);
    auto ShouldExit() -> bool;

    auto FrameBegin() -> EngineFrameBeginResult;
    auto FrameEnd() -> void;

  private:
    uint64_t m_TargetFrameDurationUs{};

    uint64_t m_LastFrameStart{};
    uint32_t m_DtUsAccum{};
    uint32_t m_FramesCounted{};
    uint32_t m_Fps{};
    bool m_ShouldExit{};
};

void Engine::Impl::Init(const EngineInitDesc &desc)
{
    uint32_t maxFps = 144;
    if (desc.maxFps > 0)
        maxFps = desc.maxFps;

    m_TargetFrameDurationUs = static_cast<uint64_t>(1'000'000.0 / maxFps);

    RhiFlags flags = None<RhiFlags>();
    if (desc.vsync)
        flags |= RhiFlags::VSync;

    g_Rhi->Init(RhiInitDesc{
        .flags = flags,
        .window = desc.window,
    });

    FrameArenaInit();

    g_StagingBuffer = CreateStagingBuffer(1 << 22);
    g_AssetManager->Init();

    m_LastFrameStart = GetMonotonicTimeMicros();
}

auto Engine::Impl::ShouldExit() -> bool
{
    return m_ShouldExit;
}

auto Engine::Impl::FrameBegin() -> EngineFrameBeginResult
{
    RhiCmdList cmd = g_Rhi->FrameBegin();

    const uint64_t frameStart = GetMonotonicTimeMicros();

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
        if (!g_Platform->PollEvent(event))
            break;

        switch (event.type)
        {
        case PlatformEventType::KeyPress: {
            g_InputManager->HandlePressed(1, event.key.code, frameStart);
            break;
        }
        case PlatformEventType::KeyRelease: {
            g_InputManager->HandleReleased(1, event.key.code, frameStart);
            break;
        }

        case PlatformEventType::MousePress: {
            g_InputManager->HandlePressed(2, event.mouse.code, frameStart);
            break;
        }
        case PlatformEventType::MouseRelease: {
            g_InputManager->HandleReleased(2, event.mouse.code, frameStart);
            break;
        }

        case PlatformEventType::ShouldExit: {
            m_ShouldExit = true;
            break;
        }

        case PlatformEventType::ShouldRedraw:
        case PlatformEventType::None:
            break;
        }
    }
    g_InputManager->Update();

    g_TweenManager->Update(dt);
    StagingBufferReset(g_StagingBuffer);
    g_AssetManager->Upload(cmd);

    return {
        .cmd = cmd,
        .dt = dt,
        .fps = m_Fps,
    };
}

auto Engine::Impl::FrameEnd() -> void
{
    g_Rhi->FrameEnd();

    uint64_t frameEnd = GetMonotonicTimeMicros();
    uint64_t frameDurationUs = frameEnd - m_LastFrameStart;

    if (m_TargetFrameDurationUs > frameDurationUs)
    {
        using namespace std::chrono_literals;
        std::this_thread::sleep_for((m_TargetFrameDurationUs - frameDurationUs) * 1us);
    }
}

//

void Engine::Init(const EngineInitDesc &desc)
{
    NYLA_ASSERT(!m_Impl);
    m_Impl = new Impl{};
    m_Impl->Init(desc);
}

auto Engine::ShouldExit() -> bool
{
    return m_Impl->ShouldExit();
}

auto Engine::FrameBegin() -> EngineFrameBeginResult
{
    return m_Impl->FrameBegin();
}

auto Engine::FrameEnd() -> void
{
    return m_Impl->FrameEnd();
}

GpuStagingBuffer *g_StagingBuffer;
Engine *g_Engine = new Engine{};

} // namespace nyla