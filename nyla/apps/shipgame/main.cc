
#include <cstdint>

#include "nyla/apps/shipgame/shipgame.h"
#include "nyla/apps/shipgame/world_renderer.h"
#include "nyla/commons/containers/map.h"
#include "nyla/commons/containers/set.h"
#include "nyla/commons/logging/init.h"
#include "nyla/commons/memory/temp.h"
#include "nyla/commons/signal/signal.h"
#include "nyla/fwk/dbg_text_renderer.h"
#include "nyla/fwk/render_pipeline.h"
#include "nyla/fwk/staging.h"
#include "nyla/platform/key_physical.h"
#include "nyla/platform/platform.h"
#include "nyla/vulkan/vulkan.h"
#include "xcb/xcb.h"

namespace nyla {

uint16_t ientity;

static uint32_t window;

static int Main() {
  LoggingInit();
  TArenaInit();
  SigIntCoreDump();

  PlatformInit();
  window = PlatformCreateWindow();

  PlatformMapInputBegin();
  {
    PlatformMapInput(kUp, KeyPhysical::E);
    PlatformMapInput(kLeft, KeyPhysical::S);
    PlatformMapInput(kDown, KeyPhysical::D);
    PlatformMapInput(kRight, KeyPhysical::F);
    PlatformMapInput(kFire, KeyPhysical::J);
    PlatformMapInput(kBoost, KeyPhysical::K);
    PlatformMapInput(kBrake, KeyPhysical::Space);
    PlatformMapInput(kZoomMore, KeyPhysical::U);
    PlatformMapInput(kZoomLess, KeyPhysical::I);
  }
  PlatformMapInputEnd();

  Vulkan_Initialize("shipgame");

  ShipgameInit();

  for (;;) {
    PlatformProcessEvents();
    if (PlatformShouldExit()) break;

    if (RecompileShadersIfNeeded()) {
      RpInit(world_pipeline);
      RpInit(dbg_text_pipeline);
      RpInit(grid_pipeline);
    }

    Vulkan_FrameBegin();
    {
      ShipgameProcess();
      Vulkan_RenderingBegin();
      ShipgameRender();
      Vulkan_RenderingEnd();
    }
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
};