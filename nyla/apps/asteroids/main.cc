#include "nyla/apps/asteroids/asteroids.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/signal/signal.h"
#include "nyla/commons0/frame_arena.h"

namespace nyla
{

static auto Main() -> int
{
    LoggingInit();
    engine0_internal::FrameArenaInit();
    SigIntCoreDump();

    PlatformInit();
    PlatformWindow window = PlatformCreateWindow();

    RhiInit({
        .window = window,
    });
    AsteroidsInit();

    for (;;)
    {
    }

    return 0;
}

} // namespace nyla

auto main() -> int
{
    return nyla::Main();
}