#include <cstdint>

#include "nyla/apps/breakout/breakout.h"
#include "nyla/apps/breakout/world_renderer.h"
#include "nyla/commons/containers/map.h"
#include "nyla/commons/containers/set.h"
#include "nyla/commons/debug/debugger.h"
#include "nyla/commons/logging/init.h"
#include "nyla/commons/memory/temp.h"
#include "nyla/commons/signal/signal.h"
#include "nyla/fwk/gui.h"
#include "nyla/platform/key_physical.h"
#include "nyla/platform/platform.h"
#include "nyla/vulkan/dbg_text_renderer.h"
#include "nyla/vulkan/render_pipeline.h"
#include "nyla/vulkan/vulkan.h"
#include "xcb/xcb.h"

namespace nyla {

static uint32_t window;

static int Main() {
  LoggingInit();
  TArenaInit();
  SigIntCoreDump();

  PlatformInit();
  window = PlatformCreateWindow();

  PlatformMapInputBegin();
  {
    PlatformMapInput(kLeft, KeyPhysical::S);
    PlatformMapInput(kRight, KeyPhysical::F);
    PlatformMapInput(kFire, KeyPhysical::J);
    PlatformMapInput(kBoost, KeyPhysical::K);
  }
  PlatformMapInputEnd();

  const char* shader_watch_dirs[] = {
      "nyla/apps/breakout/shaders", "nyla/apps/breakout/shaders/build",  //
      "nyla/fwk/shaders",           "nyla/fwk/shaders/build",            //
      "nyla/vulkan/shaders",        "nyla/vulkan/shaders/build",         //
  };
  Vulkan_Initialize("breakout", shader_watch_dirs);

  BreakoutInit();

  for (;;) {
    if (vk.shaders_invalidated) {
      RpInit(dbg_text_pipeline);
      RpInit(gui_pipeline);
      RpInit(world_pipeline);

      vk.shaders_invalidated = false;
    }

    PlatformProcessEvents();
    if (PlatformShouldExit()) {
      break;
    }

    Vulkan_FrameBegin();
    BreakoutProcess();

    Vulkan_RenderingBegin();
    BreakoutRender();
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