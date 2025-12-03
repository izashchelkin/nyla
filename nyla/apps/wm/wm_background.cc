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

namespace nyla {

using namespace platform_x11_internal;

xcb_window_t background_window;

void DrawBackground(uint32_t num_clients, std::string_view bar_text) {
  if (RecompileShadersIfNeeded()) {
    RpInit(dbg_text_pipeline);
    RpInit(gui_pipeline);
  }

  RhiFrameBegin();
  UI_FrameBegin();

  RpBegin(dbg_text_pipeline);
  DbgText(1, 1, bar_text);

  RpBegin(gui_pipeline);

  for (uint32_t i = 0; i < num_clients; ++i) {
    UI_BoxBegin(25 + 25 * i, 45, 20, 20);
  }

  RhiFrameEnd();
}

std::future<void> InitWMBackground() {
  background_window =
      X11_CreateWindow(x11.screen->width_in_pixels, x11.screen->height_in_pixels, true, XCB_EVENT_MASK_EXPOSURE);
  xcb_configure_window(x11.conn, background_window, XCB_CONFIG_WINDOW_STACK_MODE, (uint32_t[]){XCB_STACK_MODE_BELOW});
  X11_Flush();

  return std::async(std::launch::async, [] {
    RhiInit(RhiDesc{
        .window = PlatformWindow{background_window},
    });
  });
}

}  // namespace nyla