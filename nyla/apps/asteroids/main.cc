#include "absl/log/check.h"
#include "nyla/apps/asteroids/asteroids.h"
#include "nyla/commons/logging/init.h"
#include "nyla/commons/memory/temp.h"
#include "nyla/commons/signal/signal.h"
#include "nyla/vulkan/vulkan.h"
#include "nyla/x11/x11.h"

namespace nyla {

static xcb_window_t window;

static int Main() {
  LoggingInit();
  TArenaInit();
  SigIntCoreDump();
  X11_Initialize();

  {
    window = xcb_generate_id(x11.conn);

    CHECK(!xcb_request_check(
        x11.conn, xcb_create_window_checked(
                      x11.conn, XCB_COPY_FROM_PARENT, window, x11.screen->root, 0, 0, x11.screen->width_in_pixels,
                      x11.screen->height_in_pixels, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, x11.screen->root_visual,
                      XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK,
                      (uint32_t[]){false, XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
                                              XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE})));

    xcb_map_window(x11.conn, window);
    xcb_flush(x11.conn);
  }

  const char* shader_watch_dirs[] = {
      "nyla/apps/asteroids/shaders", "nyla/apps/asteroids/shaders/build", "nyla/fwk/shaders", "nyla/fwk/shaders/build",
      "nyla/vulkan/shaders",         "nyla/vulkan/shaders/build",
  };
  Vulkan_Initialize("breakout", shader_watch_dirs);

  AsteroidsInit();

  for (;;) {
  }

  return 0;
}

}  // namespace nyla

int main() {
  return nyla::Main();
}
