#include <cstdint>

#include "nyla/commons/logging/init.h"
#include "nyla/commons/memory/temp.h"
#include "nyla/commons/signal/signal.h"
#include "nyla/fwk/gui.h"
#include "nyla/platform/platform.h"
#include "nyla/vulkan/dbg_text_renderer.h"
#include "nyla/vulkan/render_pipeline.h"
#include "nyla/vulkan/vulkan.h"

namespace nyla {

static uint32_t window;

static int Main() {
  LoggingInit();
  TArenaInit();
  SigIntCoreDump();
  PlatformInit();

  window = PlatformCreateWindow();

  const char* shader_watch_dirs[] = {
      "nyla/fwk/shaders",
      "nyla/fwk/shaders/build",
      "nyla/vulkan/shaders",
      "nyla/vulkan/shaders/build",
  };
  Vulkan_Initialize("guidemo", shader_watch_dirs);

  for (;;) {
    if (vk.shaders_invalidated) {
      RpInit(gui_pipeline);
      RpInit(dbg_text_pipeline);

      vk.shaders_invalidated = false;
    }

    PlatformProcessEvents();
    if (PlatformShouldExit()) {
      break;
    }

    Vulkan_FrameBegin();

    UI_FrameBegin();

    Vulkan_RenderingBegin();
    {
      RpBegin(gui_pipeline);
      UI_BoxBegin(50, 50, 200, 120);
      UI_BoxBegin(-50, 50, 200, 120);
      UI_BoxBegin(-50, -50, 200, 120);
      UI_BoxBegin(50, -50, 200, 120);
      UI_Text("Hello world");

      RpBegin(dbg_text_pipeline);
      DbgText(10, 10, "fps= " + std::to_string(int(1.f / vk.current_frame_data.dt)));
    }
    Vulkan_RenderingEnd();

    Vulkan_FrameEnd();
  }

  return 0;
}

VkExtent2D Vulkan_PlatformGetWindowExtent() {
  const PlatformWindowSize size = PlatformGetWindowSize(window);
  return {size.width, size.height};
}

void Vulkan_PlatformSetSurface() {
  const VkXcbSurfaceCreateInfoKHR surface_create_info{
      .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
      .connection = xcb_connect(nullptr, nullptr),
      .window = window,
  };
  VK_CHECK(vkCreateXcbSurfaceKHR(vk.instance, &surface_create_info, nullptr, &vk.surface));
}

}  // namespace nyla

int main() {
  return nyla::Main();
}