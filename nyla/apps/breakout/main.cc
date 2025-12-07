#include "nyla/apps/breakout/breakout.h"
#include "nyla/apps/breakout/world_renderer.h"
#include "nyla/engine0/dbg_text_renderer.h"
#include "nyla/engine0/engine0.h"
#include "nyla/engine0/gui.h"
#include "nyla/engine0/render_pipeline.h"
#include "nyla/engine0/staging.h"
#include "nyla/platform/key_physical.h"
#include "nyla/platform/platform.h"

namespace nyla
{

static auto Main() -> int
{
    Engine0Init();

    PlatformMapInputBegin();
    PlatformMapInput(kLeft, KeyPhysical::S);
    PlatformMapInput(kRight, KeyPhysical::F);
    PlatformMapInput(kFire, KeyPhysical::J);
    PlatformMapInput(kBoost, KeyPhysical::K);
    PlatformMapInputEnd();

    BreakoutInit();

    while (!Engine0ShouldExit())
    {
        Engine0FrameBegin();

        if (RecompileShadersIfNeeded())
        {
            RpInit(worldPipeline);
            RpInit(guiPipeline);
            RpInit(dbgTextPipeline);
        }

        BreakoutFrame(Engine0GetDt(), Engine0GetFps());

        Engine0FrameEnd();
    }

    return 0;
}

} // namespace nyla

auto main() -> int
{
    return nyla::Main();
}