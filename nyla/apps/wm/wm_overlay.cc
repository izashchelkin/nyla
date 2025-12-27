#include "nyla/apps/wm/wm_overlay.h"
#include "absl/log/log.h"
#include "absl/time/clock.h"
#include "nyla/commons/logging/init.h"
#include "nyla/commons/os/clock.h"
#include "nyla/commons/signal/signal.h"
#include "nyla/platform/linux/platform_linux.h"
#include "nyla/platform/platform.h"

#include <format>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdint>

#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_pass.h"
#include "nyla/rhi/rhi_texture.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

#include "nyla/engine/debug_text_renderer.h"
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
    LoggingInit();
    SigIntCoreDump();

    g_Platform->Init({});
    Platform::Impl *x11 = g_Platform->GetImpl();

    const xcb_window_t window = x11->CreateWindow(x11->GetScreen()->width_in_pixels, x11->GetScreen()->height_in_pixels,
                                                  true, XCB_EVENT_MASK_EXPOSURE);
    xcb_configure_window(x11->GetConn(), window, XCB_CONFIG_WINDOW_STACK_MODE, (uint32_t[]){XCB_STACK_MODE_BELOW});
    x11->Flush();

    RhiInit(RhiDesc{
        .window = {window},
    });

    DebugTextRenderer *debugTextRenderer = CreateDebugTextRenderer();

    for (;;)
    {
        RhiCmdList cmd = RhiFrameBegin();

        bool shouldRedraw = false;

        auto processEvents = [&] -> void {
            for (;;)
            {
                PlatformEvent event{};
                if (!x11->PollEvent(event))
                    break;

                if (event.type == PlatformEventType::ShouldRedraw)
                    shouldRedraw = true;
                if (event.type == PlatformEventType::ShouldExit)
                    exit(0);
            }
        };
        processEvents();

        static uint64_t prevUs = GetMonotonicTimeMicros();
        if (!shouldRedraw)
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

                std::array<pollfd, 1> fds{pollfd{
                    .fd = xcb_get_file_descriptor(x11->GetConn()),
                    .events = POLLIN,
                }};

                int pollRes = poll(fds.data(), fds.size(), std::max(1, static_cast<int>(diff / 1000)));
                LOG(INFO) << pollRes;
                if (pollRes > 0)
                {
                    processEvents();
                    if (shouldRedraw)
                        break;
                }
                continue;
            }
        }

        DebugText(1, 1, std::format("{}", absl::FormatTime("%H:%M:%S %d.%m.%Y", absl::Now(), absl::LocalTimeZone())));

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