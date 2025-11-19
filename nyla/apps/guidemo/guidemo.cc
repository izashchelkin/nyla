#include <cstdint>

#include "absl/cleanup/cleanup.h"
#include "nyla/commons/logging/init.h"
#include "nyla/fwk/gui.h"
#include "nyla/vulkan/vulkan.h"
#include "nyla/x11/x11.h"
#include "xcb/xproto.h"

namespace nyla {

static bool running = true;
static xcb_window_t window;

VkExtent2D Vulkan_PlatformGetWindowExtent() {
  xcb_get_geometry_reply_t* window_geometry =
      xcb_get_geometry_reply(x11.conn, xcb_get_geometry(x11.conn, window), nullptr);

  return {.width = window_geometry->width, .height = window_geometry->height};
}

void Vulkan_PlatformSetSurface() {
  const VkXcbSurfaceCreateInfoKHR surface_create_info{
      .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
      .connection = x11.conn,
      .window = window,
  };
  VK_CHECK(vkCreateXcbSurfaceKHR(vk.instance, &surface_create_info, nullptr, &vk.surface));
}

static void ProcessXEvents() {
  for (;;) {
    if (xcb_connection_has_error(x11.conn)) {
      running = false;
      break;
    }

    xcb_generic_event_t* event = xcb_poll_for_event(x11.conn);
    if (!event) break;

    absl::Cleanup event_freer = [=]() { free(event); };
    const uint8_t event_type = event->response_type & 0x7F;

    switch (event_type) {
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

  const char* shader_watch_dirs[] = {"nyla/apps/shipgame/shaders", "nyla/apps/shipgame/shaders/build"};
  Vulkan_Initialize(shader_watch_dirs);

  RpInit(gui_pipeline);

  for (;;) {
    ProcessXEvents();
    if (!running) {
      break;
    }

    Vulkan_FrameBegin();

    if (vk.shaders_invalidated) {
      RpInit(gui_pipeline);
      vk.shaders_invalidated = false;
    }

    Vulkan_RenderingBegin();
    {
    }
    Vulkan_RenderingEnd();

    Vulkan_FrameEnd();
  }

  return 0;
}

}  // namespace nyla

int main() {
  return nyla::Main();
}