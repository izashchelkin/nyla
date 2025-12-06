#include <cstdint>

#include "nyla/commons/logging/init.h"
#include "nyla/commons/memory/temp.h"
#include "nyla/commons/signal/signal.h"
#include "nyla/engine0/dbg_text_renderer.h"
#include "nyla/engine0/gui.h"
#include "nyla/engine0/render_pipeline.h"
#include "nyla/engine0/staging.h"
#include "nyla/platform/platform.h"

namespace nyla
{

static auto Main() -> int
{
    LoggingInit();
    TArenaInit();
    SigIntCoreDump();

    PlatformInit();
    PlatformWindow window = PlatformCreateWindow();

    RhiInit(RhiDesc{
        .window = window,
    });

    for (;;)
    {
        PlatformProcessEvents();
        if (PlatformShouldExit())
        {
            break;
        }

        if (RecompileShadersIfNeeded())
        {
            RpInit(guiPipeline);
            RpInit(dbgTextPipeline);
        }

        RhiFrameBegin();

        static uint32_t fps;
        float dt;
        UpdateDtFps(fps, dt);

        RpBegin(guiPipeline);
        UiFrameBegin();

        UiBoxBegin(50, 50, 200, 120);
        UiBoxBegin(-50, 50, 200, 120);
        UiBoxBegin(-50, -50, 200, 120);
        UiBoxBegin(50, -50, 200, 120);
        UiText("Hello world");

        RpBegin(dbgTextPipeline);
        DbgText(10, 10, "fps= " + std::to_string(fps));

        RhiFrameEnd();
    }

    return 0;
}

} // namespace nyla

auto main() -> int
{
    return nyla::Main();
}