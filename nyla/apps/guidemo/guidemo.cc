#include "nyla/engine0/dbg_text_renderer.h"
#include "nyla/engine0/engine0.h"
#include "nyla/engine0/gui.h"
#include "nyla/engine0/render_pipeline.h"
#include "nyla/engine0/staging.h"

namespace nyla
{

static auto Main() -> int
{
    Engine0Init();

    while (!Engine0ShouldExit())
    {
        Engine0FrameBegin();

        if (RecompileShadersIfNeeded())
        {
            RpInit(guiPipeline);
            RpInit(dbgTextPipeline);
        }

        RpBegin(guiPipeline);
        UiFrameBegin();

        UiBoxBegin(50, 50, 200, 120);
        UiBoxBegin(-50, 50, 200, 120);
        UiBoxBegin(-50, -50, 200, 120);
        UiBoxBegin(50, -50, 200, 120);
        UiText("Hello world");

        RpBegin(dbgTextPipeline);
        DbgText(10, 10, "fps= " + std::to_string(Engine0GetFps()));

        Engine0FrameEnd();
    }

    return 0;
}

} // namespace nyla

auto main() -> int
{
    return nyla::Main();
}