#include "nyla/engine0/engine0.h"
#include "nyla/commons/logging/init.h"
#include "nyla/commons/os/clock.h"
#include "nyla/commons/signal/signal.h"
#include "nyla/engine0/frame_arena.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include <cstdint>
#include <thread>

namespace nyla
{

using namespace engine0_internal;

namespace
{

uint32_t maxFps = 144;
uint32_t fps = 0;
float dt = 0;
auto targetFrameDurationUs = static_cast<uint64_t>(1'000'000.0 / maxFps);

uint64_t frameStart = 0;

} // namespace

auto Engine0Init() -> void
{
    LoggingInit();
    FrameArenaInit();

    SigIntCoreDump();

    PlatformInit();
    PlatformWindow window = PlatformCreateWindow();

    RhiInit(RhiDesc{
        .window = window,
    });
}

auto Engine0ShouldExit() -> bool
{
    return PlatformShouldExit();
}

auto Engine0FrameBegin() -> void
{
    RhiFrameBegin();

    frameStart = GetMonotonicTimeMicros();

    static uint64_t lastUs = frameStart;
    static uint64_t dtUsAccum = 0;
    static uint32_t numFramesCounted = 0;

    const uint64_t dtUs = frameStart - lastUs;
    lastUs = frameStart;

    dt = static_cast<float>(dtUs) * 1e-6f;

    dtUsAccum += dtUs;
    ++numFramesCounted;

    constexpr uint64_t kSecondInUs = 1'000'000ull;
    if (dtUsAccum >= kSecondInUs / 2)
    {
        const double seconds = static_cast<double>(dtUsAccum) / 1'000'000.0;
        const double fpsF64 = numFramesCounted / seconds;

        fps = std::lround(fpsF64);

        dtUsAccum = 0;
        numFramesCounted = 0;
    }

    PlatformProcessEvents();
}

auto Engine0GetDt() -> float
{
    return dt;
}

auto Engine0GetFps() -> uint32_t
{
    return fps;
}

auto Engine0FrameEnd() -> void
{
    RhiFrameEnd();

    uint64_t frameEnd = GetMonotonicTimeMicros();
    uint64_t frameDurationUs = frameEnd - frameStart;

    if (targetFrameDurationUs > frameDurationUs)
    {
        using namespace std::chrono_literals;
        std::this_thread::sleep_for((targetFrameDurationUs - frameDurationUs) * 1us);
    }
}

} // namespace nyla