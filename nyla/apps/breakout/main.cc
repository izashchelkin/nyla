#include "nyla/apps/breakout/breakout.h"
#include "nyla/commons/logging/init.h"
#include "nyla/commons/signal/signal.h"
#include "nyla/engine/asset_manager.h"
#include "nyla/engine/debug_text_renderer.h"
#include "nyla/engine/engine.h"
#include "nyla/engine/renderer2d.h"
#include "nyla/engine/staging_buffer.h"
#include "nyla/engine/tween_manager.h"
#include "nyla/platform/key_physical.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_pass.h"
#include "nyla/rhi/rhi_texture.h"

namespace nyla
{

static auto Main() -> int
{
    LoggingInit();
    SigIntCoreDump();

    PlatformInit({
        .keyboardInput = true,
        .mouseInput = false,
    });
    PlatformWindow window = PlatformCreateWindow();

    EngineInit({.window = window});

    PlatformMapInputBegin();
    PlatformMapInput(kLeft, KeyPhysical::S);
    PlatformMapInput(kRight, KeyPhysical::F);
    PlatformMapInput(kFire, KeyPhysical::J);
    PlatformMapInput(kBoost, KeyPhysical::K);
    PlatformMapInputEnd();

    BreakoutAssets assets = InitBreakoutAssets();

    Renderer2D *renderer2d = CreateRenderer2D();
    DebugTextRenderer *debugTextRenderer = CreateDebugTextRenderer();

    BreakoutInit(tweenManager);

    while (!EngineShouldExit())
    {
        StagingBufferReset(stagingBuffer);

        RhiCmdList cmd = EngineFrameBegin();
        DebugText(500, 10, std::format("fps={}", EngineGetFps()));

        assetManager->Upload(cmd);

        const float dt = EngineGetDt();
        tweenManager->Update(dt);
        BreakoutProcess(dt, tweenManager);

        Renderer2DFrameBegin(cmd, renderer2d, stagingBuffer);

        RhiTexture colorTarget = RhiGetBackbufferTexture();
        RhiTextureInfo colorTargetInfo = RhiGetTextureInfo(colorTarget);

        RhiPassBegin({
            .colorTarget = RhiGetBackbufferTexture(),
            .state = RhiTextureState::ColorTarget,
        });

        BreakoutRenderGame(cmd, renderer2d, colorTargetInfo, assets);

        Renderer2DDraw(cmd, renderer2d, colorTargetInfo.width, colorTargetInfo.height, 64);
        DebugTextRendererDraw(cmd, debugTextRenderer);

        RhiPassEnd({
            .colorTarget = RhiGetBackbufferTexture(),
            .state = RhiTextureState::Present,
        });

        EngineFrameEnd();
    }

    return 0;
}

} // namespace nyla

auto main() -> int
{
    return nyla::Main();
}