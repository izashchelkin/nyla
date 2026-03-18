#include "nyla/alloc/region_alloc.h"
#include "nyla/apps/breakout/breakout.h"
#include "nyla/engine/debug_text_renderer.h"
#include "nyla/engine/engine.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"

namespace nyla
{

auto PlatformMain(std::span<const char *> argv) -> int
{
    Platform::Init({
        .enabledFeatures = PlatformFeature::Gfx | PlatformFeature::KeyboardInput,
        .open = true,
    });

    RegionAlloc rootAlloc;
    rootAlloc.Init(nullptr, 64_GiB, true);

    Engine::Init({
        .rootAlloc = &rootAlloc,
    });

    GameInit();

    while (!Engine::ShouldExit())
    {
        const auto [cmd, dt, fps] = Engine::FrameBegin();
        DebugTextRenderer::Fmt(500, 10, "fps=%d", fps);

        GameProcess(cmd, dt);

        RhiRenderTargetView rtv = Rhi::GetBackbufferView();
        GameRender(cmd, rtv);

        Engine::FrameEnd();
    }

    return 0;
}

} // namespace nyla