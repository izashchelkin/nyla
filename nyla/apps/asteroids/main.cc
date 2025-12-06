#include "nyla/apps/asteroids/asteroids.h"
#include "nyla/commons/logging/init.h"
#include "nyla/commons/memory/temp.h"
#include "nyla/commons/signal/signal.h"
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