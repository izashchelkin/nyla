#include "nyla/apps/breakout/breakout.h"
#include "nyla/commons/logging/init.h"
#include "nyla/commons/signal/signal.h"
#include "nyla/engine/debug_text_renderer.h"
#include "nyla/engine/engine.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_texture.h"

namespace nyla
{

static auto Main() -> int
{
    LoggingInit();
    SigIntCoreDump();

    g_Platform->Init({
        .enabledFeatures = PlatformFeature::KeyboardInput,
    });
    PlatformWindow window = g_Platform->CreateWindow();

    EngineInit({.window = window});

    GameInit();

    while (!EngineShouldExit())
    {
        const auto [cmd, dt, fps] = EngineFrameBegin();
        DebugText(500, 10, std::format("fps={}", fps));

        GameProcess(cmd, dt);

        RhiTexture colorTarget = RhiGetBackbufferTexture();
        GameRender(cmd, colorTarget);

        EngineFrameEnd();
    }

    return 0;
}

} // namespace nyla

auto main() -> int
{
    return nyla::Main();
}