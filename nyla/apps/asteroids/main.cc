#include "nyla/apps/asteroids/asteroids.h"
#include "nyla/commons/signal/signal.h"
#include "nyla/engine0/frame_arena.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"

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