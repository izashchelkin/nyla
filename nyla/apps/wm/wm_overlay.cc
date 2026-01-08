#include "nyla/apps/wm/wm_overlay.h"
#include "nyla/commons/os/clock.h"
#include "nyla/commons/signal/signal.h"
#include "nyla/platform/linux/platform_linux.h"
#include "nyla/platform/platform.h"

#include <chrono>
#include <format>
#include <string>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdint>

#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_texture.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

#include "nyla/engine/debug_text_renderer.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_texture.h"

namespace nyla
{

auto PlatformMain() -> int
{
    LoggingInit();
    SigIntCoreDump();

    g_Platform->Init({});
    Platform::Impl *x11 = g_Platform->GetImpl();

    const xcb_window_t window = x11->CreateWin(x11->GetScreen()->width_in_pixels, x11->GetScreen()->height_in_pixels,
                                               true, XCB_EVENT_MASK_EXPOSURE);
    xcb_configure_window(x11->GetConn(), window, XCB_CONFIG_WINDOW_STACK_MODE, (uint32_t[]){XCB_STACK_MODE_BELOW});
    x11->Flush();

    g_Rhi->Init(RhiInitDesc{
        .window = {window},
        .flags = RhiFlags::VSync,
        .limits =
            {
                .numTextures = 0,
                .numTextureViews = 0,
                .numBuffers = 0,
                .numSamplers = 0,

                .numFramesInFlight = 1,
                .maxDrawCount = 1,
                .maxPassCount = 1,

                .frameConstantSize = 0,
                .passConstantSize = 0,
                .drawConstantSize = 0,
                .largeDrawConstantSize = 320,
            },
    });

    DebugTextRenderer *debugTextRenderer = CreateDebugTextRenderer();

    for (;;)
    {
        RhiCmdList cmd = g_Rhi->FrameBegin();

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
                if (diff >= 500'000)
                {
                    prevUs = now;
                    break;
                }

                std::array<pollfd, 1> fds{pollfd{
                    .fd = xcb_get_file_descriptor(x11->GetConn()),
                    .events = POLLIN,
                }};

                int pollRes = poll(fds.data(), fds.size(), std::max(1, static_cast<int>(diff / 1000)));
                if (pollRes > 0)
                {
                    processEvents();
                    if (shouldRedraw)
                        break;
                }
                continue;
            }
        }

        auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());

        std::string barText =
            std::format("{:%H:%M:%S %d.%m.%Y}", std::chrono::zoned_time{std::chrono::current_zone(), now});

        DebugText(1, 1, barText);

        g_Rhi->PassBegin({
            .renderTarget = g_Rhi->GetBackbufferView(),
            .state = RhiTextureState::ColorTarget,
        });

        DebugTextRendererDraw(cmd, debugTextRenderer);

        g_Rhi->PassEnd({
            .renderTarget = g_Rhi->GetBackbufferView(),
            .state = RhiTextureState::Present,
        });

        g_Rhi->FrameEnd();
    }

    return 0;
}

} // namespace nyla