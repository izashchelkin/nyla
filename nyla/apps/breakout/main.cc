#include "nyla/apps/breakout/breakout.h"
#include "nyla/apps/breakout/world_renderer.h"
#include "nyla/engine0/dbg_text_renderer.h"
#include "nyla/engine0/engine0.h"
#include "nyla/engine0/gui.h"
#include "nyla/engine0/render_pipeline.h"
#include "nyla/engine0/staging.h"
#include "nyla/platform/key_physical.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_pass.h"
#include "nyla/rhi/rhi_texture.h"
#include <cstdint>

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

    std::array<RhiTexture, kRhiMaxNumFramesInFlight> offscreenTextures;
    for (uint32_t i = 0; i < RhiGetNumFramesInFlight(); ++i)
    {
        offscreenTextures[i] = RhiCreateTexture(RhiTextureDesc{
            .width = 1000,
            .height = 1000,
            .memoryUsage = RhiMemoryUsage::GpuOnly,
            .format = RhiTextureFormat::R8G8B8A8_sRGB,
        });
    }

    while (!Engine0ShouldExit())
    {
        Engine0FrameBegin();

        if (RecompileShadersIfNeeded())
        {
            RpInit(worldPipeline);
            RpInit(guiPipeline);
            RpInit(dbgTextPipeline);
        }

        RhiTexture offscreenTexture = offscreenTextures[RhiFrameGetIndex()];

        RhiPassBegin({
            .colorTarget = offscreenTexture,
        });

        BreakoutFrame(Engine0GetDt(), Engine0GetFps());

        RhiPassEnd({
            .colorTarget = offscreenTexture,
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