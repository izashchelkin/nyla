#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "nyla/apps/breakout/breakout.h"
#include "nyla/apps/breakout/world_renderer.h"
#include "nyla/commons/containers/map.h"
#include "nyla/commons/containers/set.h"
#include "nyla/commons/debug/debugger.h"
#include "nyla/commons/logging/init.h"
#include "nyla/commons/memory/temp.h"
#include "nyla/commons/os/clock.h"
#include "nyla/fwk/gui.h"
#include "nyla/fwk/input.h"
#include "nyla/vulkan/dbg_text_renderer.h"
#include "nyla/vulkan/render_pipeline.h"
#include "nyla/vulkan/vulkan.h"
#include "nyla/x11/x11.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

static bool running = true;
static xcb_window_t window;

static void ProcessXEvents() {
  InputHandleFrame();

  for (;;) {
    if (xcb_connection_has_error(x11.conn)) {
      running = false;
      break;
    }

    xcb_generic_event_t* event = xcb_poll_for_event(x11.conn);
    if (!event) break;

    absl::Cleanup event_freer = [=]() { free(event); };
    const uint8_t event_type = event->response_type & 0x7F;

    uint64_t now = GetMonotonicTimeMicros();

    switch (event_type) {
      case XCB_KEY_PRESS: {
        auto keypress = reinterpret_cast<xcb_key_press_event_t*>(event);
        InputHandlePressed({1, keypress->detail}, now);
        break;
      }

      case XCB_KEY_RELEASE: {
        auto keyrelease = reinterpret_cast<xcb_key_release_event_t*>(event);
        InputHandleReleased({1, keyrelease->detail});
        break;
      }

      case XCB_BUTTON_PRESS: {
        auto buttonpress = reinterpret_cast<xcb_button_press_event_t*>(event);
        InputHandlePressed({2, buttonpress->detail}, now);
        break;
      }

      case XCB_BUTTON_RELEASE: {
        auto buttonrelease = reinterpret_cast<xcb_button_release_event_t*>(event);
        InputHandleReleased({2, buttonrelease->detail});
        break;
      }

      case XCB_CLIENT_MESSAGE: {
        auto clientmessage = reinterpret_cast<xcb_client_message_event_t*>(event);

        if (clientmessage->format == 32 && clientmessage->type == x11.atoms.wm_protocols &&
            clientmessage->data.data32[0] == x11.atoms.wm_delete_window) {
          running = false;
        }
        break;
      }
    }
  }
}

static int Main() {
  InitLogging();
  TArenaInit();

  {
    struct sigaction sa;
    sa.sa_handler = [](int signum) { CHECK(false); };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
      LOG(ERROR) << "sigaction failed";
      return false;
    }
  }

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
      "nyla/apps/breakout/shaders", "nyla/apps/breakout/shaders/build",  //
      "nyla/fwk/shaders",           "nyla/fwk/shaders/build",            //
      "nyla/vulkan/shaders",        "nyla/vulkan/shaders/build",         //
  };
  Vulkan_Initialize("breakout", shader_watch_dirs);

  {
    X11_KeyResolver key_resolver;
    X11_InitializeKeyResolver(key_resolver);

    InputMapId(kLeft, {1, X11_ResolveKeyCode(key_resolver, "AC02")});
    InputMapId(kRight, {1, X11_ResolveKeyCode(key_resolver, "AC04")});
    InputMapId(kFire, {1, X11_ResolveKeyCode(key_resolver, "AC07")});
    InputMapId(kBoost, {1, X11_ResolveKeyCode(key_resolver, "AC08")});

    X11_FreeKeyResolver(key_resolver);
  }

  BreakoutInit();

  for (;;) {
    if (vk.shaders_invalidated) {
      RpInit(dbg_text_pipeline);
      RpInit(gui_pipeline);
      RpInit(world_pipeline);

      vk.shaders_invalidated = false;
    }

    ProcessXEvents();
    if (!running) break;

    BreakoutProcess();

    Vulkan_FrameBegin();
    WorldSetUp();

    Vulkan_RenderingBegin();
    {
      RpBegin(world_pipeline);
      BreakoutRender();

      RpBegin(dbg_text_pipeline);
      DbgText(500, 10, "fps= " + std::to_string(GetFps()));
    }
    Vulkan_RenderingEnd();
    Vulkan_FrameEnd();
  }

  return 0;
}

static xcb_connection_t* GetVkXcbConn() {
  static xcb_connection_t* vk_xcb_conn = xcb_connect(nullptr, nullptr);
  return vk_xcb_conn;
}

VkExtent2D Vulkan_PlatformGetWindowExtent() {
  xcb_connection_t* conn = GetVkXcbConn();
  xcb_get_geometry_reply_t* window_geometry = xcb_get_geometry_reply(conn, xcb_get_geometry(conn, window), nullptr);

  return {.width = window_geometry->width, .height = window_geometry->height};
}

void Vulkan_PlatformSetSurface() {
  const VkXcbSurfaceCreateInfoKHR surface_create_info{
      .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
      .connection = GetVkXcbConn(),
      .window = window,
  };
  VK_CHECK(vkCreateXcbSurfaceKHR(vk.instance, &surface_create_info, nullptr, &vk.surface));
}

}  // namespace nyla

int main() {
  return nyla::Main();
}