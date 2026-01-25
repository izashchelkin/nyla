#include "nyla/engine/engine.h"
#include "nyla/commons/align.h"
#include "nyla/commons/assert.h"
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
    m_CpuAllocs;

    uint32_t maxFps = 144;
    if (desc.maxFps > 0)
        maxFps = desc.maxFps;

    m_TargetFrameDurationUs = static_cast<uint64_t>(1'000'000.0 / maxFps);

    RhiFlags flags = None<RhiFlags>();
    if (desc.vsync)
        flags |= RhiFlags::VSync;

    g_Rhi->Init(RhiInitDesc{
        .flags = flags,
    });

    g_StagingBuffer = CreateStagingBuffer(1 << 22);
    g_AssetManager->Init();

    m_LastFrameStart = g_Platform->GetMonotonicTimeMicros();

#if 0
    m_StaticVertexBufferSize = 1_GiB;
    m_StaticVertexBufferAt = 0;
    m_StaticVertexBuffer = g_Rhi->CreateBuffer({
        .size = m_StaticVertexBufferSize,
        .bufferUsage = RhiBufferUsage::Vertex,
        .memoryUsage = RhiMemoryUsage::GpuOnly,
    });
#endif
}

auto Engine::ShouldExit() -> bool
{
    return m_ShouldExit;
}

auto Engine::FrameBegin() -> EngineFrameBeginResult
{
    RhiCmdList cmd = g_Rhi->FrameBegin();

    const uint64_t frameStart = g_Platform->GetMonotonicTimeMicros();

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
        case PlatformEventType::KeyDown: {
            g_InputManager->HandlePressed(1, uint32_t(event.key), frameStart);
            break;
        }
        case PlatformEventType::KeyUp: {
            g_InputManager->HandleReleased(1, uint32_t(event.key), frameStart);
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

        case PlatformEventType::WinResize: {
            g_Rhi->TriggerSwapchainRecreate();
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

auto Engine::FrameEnd() -> void
{
    g_Rhi->FrameEnd();

    uint64_t frameEnd = g_Platform->GetMonotonicTimeMicros();
    uint64_t frameDurationUs = frameEnd - m_LastFrameStart;

    if (m_TargetFrameDurationUs > frameDurationUs)
    {
        using namespace std::chrono_literals;
        std::this_thread::sleep_for((m_TargetFrameDurationUs - frameDurationUs) * 1us);
    }
}

auto GpuLinearAllocBuffer::SubAlloc(uint32_t size) -> GpuBufferSubAlloc
{
    GpuBufferSubAlloc ret;
    ret.size = size;

    AlignUp<uint64_t>(m_At, 16);
    ret.offset = m_At;
    m_At += size;

    NYLA_ASSERT(m_At <= g_Rhi->GetBufferSize(m_Buffer));
    return ret;
}

//

Engine *g_Engine;
GpuUploadManager *g_StagingBuffer;

} // namespace nyla