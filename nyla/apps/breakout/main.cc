#include "nyla/apps/breakout/breakout.h"
#include "nyla/apps/breakout/world_renderer.h"
#include "nyla/commons/logging/init.h"
#include "nyla/commons/signal/signal.h"
#include "nyla/engine0/debug_text_renderer.h"
#include "nyla/engine0/engine0.h"
#include "nyla/engine0/renderer2d.h"
#include "nyla/engine0/staging_buffer.h"
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

    Engine0Init({.window = window});

    PlatformMapInputBegin();
    PlatformMapInput(kLeft, KeyPhysical::S);
    PlatformMapInput(kRight, KeyPhysical::F);
    PlatformMapInput(kFire, KeyPhysical::J);
    PlatformMapInput(kBoost, KeyPhysical::K);
    PlatformMapInputEnd();

    BreakoutInit();

    // std::array<RhiTexture, kRhiMaxNumFramesInFlight> offscreenTextures;
    // for (uint32_t i = 0; i < RhiGetNumFramesInFlight(); ++i)
    // {
    //     offscreenTextures[i] = RhiCreateTexture(RhiTextureDesc{
    //         .width = 1000,
    //         .height = 1000,
    //         .memoryUsage = RhiMemoryUsage::GpuOnly,
    //         .usage = RhiTextureUsage::ColorTarget | RhiTextureUsage::ShaderSampled,
    //         .format = RhiTextureFormat::B8G8R8A8_sRGB,
    //     });
    // }
    //
    // if (RecompileShadersIfNeeded())
    // {
    //     RpInit(worldPipeline);
    //     RpInit(guiPipeline);
    //     RpInit(dbgTextPipeline);
    // }

    // RhiTexture offscreenTexture = offscreenTextures[RhiGetFrameIndex()];

    StagingBuffer *stagingBuffer = CreateStagingBuffer(192);
    Renderer2D *renderer2d = CreateRenderer2D();
    DebugTextRenderer *debugTextRenderer = CreateDebugTextRenderer();

    while (!Engine0ShouldExit())
    {
        RhiCmdList cmd = Engine0FrameBegin();

        DebugText(500, 10, std::format("fps={}", Engine0GetFps()));

        const float dt = Engine0GetDt();
        BreakoutProcess(dt);

        StagingBufferReset(stagingBuffer);
        Renderer2DFrameBegin(cmd, renderer2d, stagingBuffer);

        RhiTexture colorTarget = RhiGetBackbufferTexture();
        RhiTextureInfo colorTargetInfo = RhiGetTextureInfo(colorTarget);

        RhiPassBegin({
            .colorTarget = RhiGetBackbufferTexture(),
            .state = RhiTextureState::ColorTarget,
        });

        BreakoutRenderGame(cmd, renderer2d, colorTargetInfo);

        Renderer2DDraw(cmd, renderer2d, colorTargetInfo.width, colorTargetInfo.height, 64);
        DebugTextRendererDraw(cmd, debugTextRenderer);

        RhiPassEnd({
            .colorTarget = RhiGetBackbufferTexture(),
            .state = RhiTextureState::Present,
        });

        Engine0FrameEnd();
    }

    return 0;
}

} // namespace nyla

auto main() -> int
{
    return nyla::Main();
}