#include <unistd.h>

#include <cstdint>

#include "nyla/apps/breakout/breakout.h"
#include "nyla/apps/breakout/world_renderer.h"
#include "nyla/commons/logging/init.h"
#include "nyla/commons/memory/temp.h"
#include "nyla/commons/signal/signal.h"
#include "nyla/fwk/dbg_text_renderer.h"
#include "nyla/fwk/gui.h"
#include "nyla/fwk/render_pipeline.h"
#include "nyla/fwk/staging.h"
#include "nyla/platform/key_physical.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"

namespace nyla
{

static auto Main() -> int
{
    LoggingInit();
    TArenaInit();
    SigIntCoreDump();

    PlatformInit();
    PlatformWindow window = PlatformCreateWindow();

    PlatformMapInputBegin();
    PlatformMapInput(kLeft, KeyPhysical::S);
    PlatformMapInput(kRight, KeyPhysical::F);
    PlatformMapInput(kFire, KeyPhysical::J);
    PlatformMapInput(kBoost, KeyPhysical::K);
    PlatformMapInputEnd();

    RhiInit(RhiDesc{
        .window = window,
    });
    BreakoutInit();

    for (;;)
    {
        PlatformProcessEvents();
        if (PlatformShouldExit())
        {
            break;
        }

        if (RecompileShadersIfNeeded())
        {
            RpInit(worldPipeline);
            RpInit(guiPipeline);
            RpInit(dbgTextPipeline);
        }

        static uint32_t fps;
        float dt;
        UpdateDtFps(fps, dt);

        RhiFrameBegin();
        BreakoutFrame(dt, fps);
        RhiFrameEnd();
    }

    return 0;
}

} // namespace nyla

auto main() -> int
{
    return nyla::Main();
}