#include <cstdint>

#include "nyla/apps/shipgame/shipgame.h"
#include "nyla/apps/shipgame/world_renderer.h"
#include "nyla/engine0/dbg_text_renderer.h"
#include "nyla/engine0/engine0.h"
#include "nyla/engine0/render_pipeline.h"
#include "nyla/engine0/staging.h"
#include "nyla/platform/key_physical.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi_pass.h"
#include "nyla/rhi/rhi_texture.h"

namespace nyla
{

uint16_t ientity;

static auto Main() -> int
{
    Engine0Init(true);

    PlatformMapInputBegin();
    PlatformMapInput(kUp, KeyPhysical::E);
    PlatformMapInput(kLeft, KeyPhysical::S);
    PlatformMapInput(kDown, KeyPhysical::D);
    PlatformMapInput(kRight, KeyPhysical::F);
    PlatformMapInput(kFire, KeyPhysical::J);
    PlatformMapInput(kBoost, KeyPhysical::K);
    PlatformMapInput(kBrake, KeyPhysical::Space);
    PlatformMapInput(kZoomMore, KeyPhysical::U);
    PlatformMapInput(kZoomLess, KeyPhysical::I);
    PlatformMapInputEnd();

    ShipgameInit();

    while (!Engine0ShouldExit())
    {
        Engine0FrameBegin();

        if (RecompileShadersIfNeeded())
        {
            RpInit(worldPipeline);
            RpInit(dbgTextPipeline);
            RpInit(gridPipeline);
        }

        RhiPassBegin({
            .colorTarget = RhiGetBackbufferTexture(),
            .state = RhiTextureState::ColorTarget,
        });

        ShipgameFrame(Engine0GetDt(), Engine0GetFps());

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
};