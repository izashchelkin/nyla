#include "nyla/apps/breakout/breakout.h"
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
    });
    g_Platform->WinOpen();

    g_Engine->Init({});

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