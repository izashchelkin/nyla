#include "nyla/apps/breakout/breakout.h"
#include "nyla/commons/memory/region_alloc.h"
#include "nyla/engine/debug_text_renderer.h"
#include "nyla/engine/engine.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_texture.h"
#include <format>

namespace nyla
{

auto PlatformMain() -> int
{
    g_Platform->Init({
        .enabledFeatures = PlatformFeature::KeyboardInput,
        .open = true,
    });

    RegionAlloc rootAlloc;
    rootAlloc.Init(nullptr, 64_GiB, RegionAllocCommitPageGrowth::GetInstance());

    CpuAllocs cpuAllocs{
        .permanent = rootAlloc.PushSubAlloc(512_MiB),
        .transient = rootAlloc.PushSubAlloc(512_MiB),
    };

    g_Engine = cpuAllocs.permanent.Push<Engine>();
    g_Engine->Init({.cpuAllocs = cpuAllocs});

    GameInit();

    while (!g_Engine->ShouldExit())
    {
        const auto [cmd, dt, fps] = g_Engine->FrameBegin();
        DebugText(500, 10, std::format("fps={}", fps));

        GameProcess(cmd, dt);

        RhiRenderTargetView rtv = g_Rhi->GetBackbufferView();
        GameRender(cmd, rtv);

        g_Engine->FrameEnd();
    }

    return 0;
}

} // namespace nyla