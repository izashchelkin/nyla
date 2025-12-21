#include "nyla/apps/breakout/breakout.h"
#include "nyla/apps/breakout/world_renderer.h"
#include "nyla/engine0/renderer2d.h"
#include "nyla/engine0/engine0.h"
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
    Engine0Init(false);

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

    StagingBuffer *stagingBuffer = CreateStagingBuffer(1 << 10);
    Renderer2D *renderer = CreateRenderer2D();

    while (!Engine0ShouldExit())
    {
        RhiCmdList cmd = Engine0FrameBegin();

        const float dt = Engine0GetDt();
        BreakoutProcess(dt);

        StagingBufferReset(stagingBuffer);
        Renderer2DFrameBegin(cmd, renderer, stagingBuffer);

        RhiTexture colorTarget = RhiGetBackbufferTexture();
        RhiTextureInfo colorTargetInfo = RhiGetTextureInfo(colorTarget);

        RhiPassBegin({
            .colorTarget = RhiGetBackbufferTexture(),
            .state = RhiTextureState::ColorTarget,
        });

        BreakoutRenderGame(cmd, renderer, colorTargetInfo);

#if 0 
        RpBegin(dbgTextPipeline);
        DbgText(500, 10, std::format("fps={} ball=({:.1f}, {:.1f})", fps, ballPos[0], ballPos[1]));
#endif

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