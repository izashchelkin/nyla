#include "nyla/apps/wm/wm_background.h"

#include <sys/poll.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstdint>
#include <future>

#include "nyla/fwk/dbg_text_renderer.h"
#include "nyla/fwk/gui.h"
#include "nyla/fwk/staging.h"
#include "nyla/platform/x11/platform_x11.h"
#include "nyla/rhi/rhi.h"
#include "xcb/xproto.h"

namespace nyla
{

using namespace platform_x11_internal;

xcb_window_t backgroundWindow;

void DrawBackground(uint32_t numClients, std::string_view barText)
{
    if (RecompileShadersIfNeeded())
    {
        RpInit(dbgTextPipeline);
        RpInit(guiPipeline);
    }

    RhiFrameBegin();
    UiFrameBegin();

    RpBegin(dbgTextPipeline);
    DbgText(1, 1, barText);

    RpBegin(guiPipeline);

    for (uint32_t i = 0; i < numClients; ++i)
    {
        UiBoxBegin(25 + 25 * i, 45, 20, 20);
    }

    RhiFrameEnd();
}

auto InitWMBackground() -> std::future<void>
{
    backgroundWindow =
        X11CreateWindow(x11.screen->width_in_pixels, x11.screen->height_in_pixels, true, XCB_EVENT_MASK_EXPOSURE);
    xcb_configure_window(x11.conn, backgroundWindow, XCB_CONFIG_WINDOW_STACK_MODE, (uint32_t[]){XCB_STACK_MODE_BELOW});
    X11Flush();

    return std::async(std::launch::async, [] -> void {
        RhiInit(RhiDesc{
            .window = PlatformWindow{backgroundWindow},
        });
    });
}

} // namespace nyla