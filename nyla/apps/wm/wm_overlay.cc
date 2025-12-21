#include "nyla/apps/wm/wm_overlay.h"
#include "nyla/commons/logging/init.h"
#include "nyla/commons/os/clock.h"
#include "nyla/commons/signal/signal.h"
#include "nyla/platform/platform.h"
#include "nyla/platform/x11/platform_x11.h"

#include <format>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

#include <cstdint>

#include "nyla/platform/x11/platform_x11.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_pass.h"
#include "nyla/rhi/rhi_texture.h"
#include "xcb/xproto.h"

#include "nyla/apps/breakout/world_renderer.h"
#include "nyla/engine0/debug_text_renderer.h"
#include "nyla/engine0/engine0.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_pass.h"
#include "nyla/rhi/rhi_texture.h"

namespace nyla
{

namespace
{

auto Main() -> int
{
    using namespace platform_x11_internal;

    LoggingInit();
    SigIntCoreDump();

    PlatformInit({});

    const xcb_window_t window =
        X11CreateWindow(x11.screen->width_in_pixels, x11.screen->height_in_pixels, true, XCB_EVENT_MASK_EXPOSURE);
    xcb_configure_window(x11.conn, window, XCB_CONFIG_WINDOW_STACK_MODE, (uint32_t[]){XCB_STACK_MODE_BELOW});
    X11Flush();

    RhiInit(RhiDesc{
        .window = {window},
    });

    DebugTextRenderer *debugTextRenderer = CreateDebugTextRenderer();

    while (!PlatformShouldExit())
    {
        RhiCmdList cmd = RhiFrameBegin();

        auto ret = PlatformProcessEvents();

        static uint64_t prevUs = GetMonotonicTimeMicros();
        if (!ret.shouldRedraw)
        {
            for (;;)
            {
                const uint64_t now = GetMonotonicTimeMicros();
                const uint64_t diff = now - prevUs;
                if (diff >= 1'000'000)
                {
                    prevUs = now;
                    break;
                }

                using namespace std::chrono_literals;
                std::this_thread::sleep_for(10us);

                auto ret = PlatformProcessEvents();
                if (ret.shouldRedraw)
                    break;
            }
        }

        static uint32_t drawCount = 0;
        DebugText(1, 1,
                  std::format("{} {}", drawCount++,
                              absl::FormatTime("%H:%M:%S %d.%m.%Y", absl::Now(), absl::LocalTimeZone())));

        RhiPassBegin({
            .colorTarget = RhiGetBackbufferTexture(),
            .state = RhiTextureState::ColorTarget,
        });

        DebugTextRendererDraw(cmd, debugTextRenderer);

        RhiPassEnd({
            .colorTarget = RhiGetBackbufferTexture(),
            .state = RhiTextureState::Present,
        });

        RhiFrameEnd();
    }

    return 0;
}

} // namespace

} // namespace nyla

auto main() -> int
{
    return nyla::Main();
}