#include "nyla/apps/wm/wm_background.h"

#include <sys/poll.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstdint>
#include <span>

#include "nyla/commons/containers/map.h"
#include "nyla/commons/logging/init.h"
#include "nyla/commons/memory/optional.h"
#include "nyla/commons/memory/temp.h"
#include "nyla/fwk/gui.h"
#include "nyla/vulkan/dbg_text_renderer.h"
#include "nyla/vulkan/vulkan.h"
#include "nyla/x11/error.h"
#include "nyla/x11/x11.h"
#include "xcb/xproto.h"

namespace nyla {

xcb_window_t background_window;

void DrawBackground(uint32_t num_clients, std::string_view bar_text) {
  if (vk.shaders_invalidated) {
    RpInit(dbg_text_pipeline);
    RpInit(gui_pipeline);
    vk.shaders_invalidated = false;
  }

  Vulkan_FrameBegin();
  UI_FrameBegin();

  Vulkan_RenderingBegin();
  {
    RpBegin(dbg_text_pipeline);
    DbgText(1, 1, bar_text);

    RpBegin(gui_pipeline);

    for (uint32_t i = 0; i < num_clients; ++i) {
      UI_BoxBegin(25 + 25 * i, 45, 20, 20);
    }
  }
  Vulkan_RenderingEnd();
  Vulkan_FrameEnd();
}

void InitWMBackground() {
  background_window =
      X11_CreateWindow(x11.screen->width_in_pixels, x11.screen->height_in_pixels, true, XCB_EVENT_MASK_EXPOSURE);
  xcb_configure_window(x11.conn, background_window, XCB_CONFIG_WINDOW_STACK_MODE, (uint32_t[]){XCB_STACK_MODE_BELOW});
  X11_Flush();

  Vulkan_Initialize("wm_background", {});

  RpInit(dbg_text_pipeline);
  RpInit(gui_pipeline);
}

#if !defined(NYLA_ENTRYPOINT)
static int Main() {
  InitLogging();
  TArenaInit();

  X11_Initialize();

  InitWMBackground();

  for (;;) {
    DrawBackground(5, "Hello world");
  }

  return 0;
}
#endif

VkExtent2D Vulkan_PlatformGetWindowExtent() {
  xcb_get_geometry_reply_t* window_geometry =
      xcb_get_geometry_reply(x11.conn, xcb_get_geometry(x11.conn, background_window), nullptr);

  return {.width = window_geometry->width, .height = window_geometry->height};
}

void Vulkan_PlatformSetSurface() {
  const VkXcbSurfaceCreateInfoKHR surface_create_info{
      .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
      .connection = x11.conn,
      .window = background_window,
  };
  VK_CHECK(vkCreateXcbSurfaceKHR(vk.instance, &surface_create_info, nullptr, &vk.surface));
}

}  // namespace nyla

#if !defined(NYLA_ENTRYPOINT)
int main() {
  return nyla::Main();
}
#endif

#if !defined(NYLA_ENTRYPOINT)
#define NYLA_ENTRYPOINT
#endif