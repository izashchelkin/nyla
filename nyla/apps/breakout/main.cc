#include "nyla/apps/breakout/breakout.h"
#include "nyla/commons/logging/init.h"
#include "nyla/commons/signal/signal.h"
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
    LoggingInit();
    SigIntCoreDump();

    g_Platform->Init({
        .enabledFeatures = PlatformFeature::KeyboardInput,
    });
    PlatformWindow window = g_Platform->CreateWin();

    g_Engine->Init({.window = window});

    GameInit();

    while (!g_Engine->ShouldExit())
    {
        const auto [cmd, dt, fps] = g_Engine->FrameBegin();
        DebugText(500, 10, std::format("fps={}", fps));

        GameProcess(cmd, dt);

        RhiTexture colorTarget = g_Rhi->GetBackbufferTexture();
        GameRender(cmd, colorTarget);

        g_Engine->FrameEnd();
    }

    return 0;
}

} // namespace nyla