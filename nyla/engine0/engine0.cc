#include "nyla/engine0/engine0.h"
#include "nyla/commons/bitenum.h"
#include "nyla/commons/os/clock.h"
#include "nyla/engine0/frame_arena.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include <cmath>
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
uint64_t targetFrameDurationUs;

uint64_t frameStart = 0;

} // namespace

auto Engine0Init(Engine0Desc desc) -> void
{
    if (desc.maxFps > 0)
        maxFps = desc.maxFps;

    targetFrameDurationUs = static_cast<uint64_t>(1'000'000.0 / maxFps);

    RhiFlags flags = None<RhiFlags>();
    if (desc.vsync)
        flags |= RhiFlags::VSync;

    RhiInit(RhiDesc{
        .flags = flags,
        .window = desc.window,
    });

    FrameArenaInit();
}

auto Engine0ShouldExit() -> bool
{
    return PlatformShouldExit();
}

auto Engine0FrameBegin() -> RhiCmdList
{
    RhiCmdList cmd = RhiFrameBegin();

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

#if 0
    static bool spvChanged = true;
    static bool srcChanged = true;

    for (PlatformFileChanged change : PlatformFsGetFileChanges())
    {
        if (change.seen)
            continue;
        change.seen = true;

        const auto &path = change.path;
        if (path.ends_with(".spv"))
        {
            spvChanged = true;
        }
        else if (path.ends_with(".hlsl"))
        {
            srcChanged = true;
        }
    }

    if (srcChanged)
    {
        LOG(INFO) << "shaders recompiling";
        system("python3 /home/izashchelkin/nyla/scripts/shaders.py");
        usleep(1e6);
        PlatformProcessEvents();

        srcChanged = false;
    }

    if (spvChanged)
    {
        spvChanged = false;
        return true;
    }
#endif

    return cmd;
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