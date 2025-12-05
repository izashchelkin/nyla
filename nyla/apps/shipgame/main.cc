#include <cstdint>

#include "nyla/apps/shipgame/shipgame.h"
#include "nyla/apps/shipgame/world_renderer.h"
#include "nyla/commons/logging/init.h"
#include "nyla/commons/memory/temp.h"
#include "nyla/commons/os/clock.h"
#include "nyla/commons/signal/signal.h"
#include "nyla/fwk/dbg_text_renderer.h"
#include "nyla/fwk/render_pipeline.h"
#include "nyla/fwk/staging.h"
#include "nyla/platform/key_physical.h"
#include "nyla/platform/platform.h"

namespace nyla
{

uint16_t ientity;

static auto Main() -> int
{
    LoggingInit();
    TArenaInit();
    SigIntCoreDump();

    PlatformInit();
    PlatformWindow window = PlatformCreateWindow();

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

    RhiInit(RhiDesc{
        .window = window,
    });
    ShipgameInit();

    for (;;)
    {
        PlatformProcessEvents();
        if (PlatformShouldExit())
            break;

        if (RecompileShadersIfNeeded())
        {
            RpInit(world_pipeline);
            RpInit(dbg_text_pipeline);
            RpInit(grid_pipeline);
        }

        static uint32_t fps;
        float dt;
        UpdateDtFps(fps, dt);

        RhiFrameBegin();
        ShipgameFrame(dt, fps);
        RhiFrameEnd();
    }

    return 0;
}

} // namespace nyla

auto main() -> int
{
    return nyla::Main();
};