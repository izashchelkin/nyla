#include "nyla/apps/wm/wm_overlay.h"
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
#include "xcb/xcb.h"
#include "xcb/xproto.h"

#include "nyla/engine/debug_text_renderer.h"
#include "nyla/platform/platform.h"

namespace nyla
{

auto PlatformMain(std::span<const char *> argv) -> int
{
    Platform::Init({
        .enabledFeatures = PlatformFeature::Gfx,
    });

    const xcb_window_t window =
        LinuxX11Platform::CreateWin(LinuxX11Platform::GetScreen()->width_in_pixels,
                                    LinuxX11Platform::GetScreen()->height_in_pixels, true, XCB_EVENT_MASK_EXPOSURE);
    xcb_configure_window(LinuxX11Platform::GetConn(), window, XCB_CONFIG_WINDOW_STACK_MODE,
                         (uint32_t[]){XCB_STACK_MODE_BELOW});
    LinuxX11Platform::Flush();

    LinuxX11Platform::SetWindow(window);

    Rhi::Init(RhiInitDesc{
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

    DebugTextRenderer debugTextRenderer;
    debugTextRenderer.Init();

    for (;;)
    {
        RhiCmdList cmd = Rhi::FrameBegin();

        bool shouldRedraw = false;

        auto processEvents = [&] -> void {
            for (;;)
            {
                PlatformEvent event{};
                if (!Platform::WinPollEvent(event))
                    break;

                if (event.type == PlatformEventType::Repaint)
                    shouldRedraw = true;
                if (event.type == PlatformEventType::Quit)
                    exit(0);
            }
        };
        processEvents();

        static uint64_t prevUs = Platform::GetMonotonicTimeMicros();
        if (!shouldRedraw)
        {
            for (;;)
            {
                const uint64_t now = Platform::GetMonotonicTimeMicros();
                const uint64_t diff = now - prevUs;
                if (diff >= 500'000)
                {
                    prevUs = now;
                    break;
                }

                std::array<pollfd, 1> fds{pollfd{
                    .fd = xcb_get_file_descriptor(LinuxX11Platform::GetConn()),
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

        debugTextRenderer.Text(1, 1, barText);

        RhiTexture backbuffer = Rhi::GetTexture(Rhi::GetBackbufferView());

        Rhi::CmdTransitionTexture(cmd, backbuffer, RhiTextureState::ColorTarget);

        Rhi::PassBegin({
            .rtv = Rhi::GetBackbufferView(),
            .dsv = {},
        });

        debugTextRenderer.CmdFlush(cmd);

        Rhi::PassEnd();

        Rhi::CmdTransitionTexture(cmd, backbuffer, RhiTextureState::Present);

        Rhi::FrameEnd();
    }

    return 0;
}

} // namespace nyla