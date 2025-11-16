#include <signal.h>

#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "nyla/commons/containers/set.h"
#include "nyla/commons/logging/init.h"
#include "nyla/commons/memory/tnew.h"
#include "nyla/commons/os/clock.h"
#include "nyla/shipgame/game.h"
#include "nyla/shipgame/world_renderer.h"
#include "nyla/vulkan/dbg_text_renderer.h"
#include "nyla/vulkan/render_pipeline.h"
#include "nyla/vulkan/vulkan.h"
#include "nyla/x11/x11.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

uint16_t ientity;

static bool running = true;
static xcb_window_t window;

static Set<xcb_keycode_t> pressed_keys;
static Set<xcb_keycode_t> released_keys;
static Set<xcb_button_index_t> pressed_buttons;
static Set<xcb_button_index_t> released_buttons;

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
      case XCB_KEY_PRESS: {
        auto keypress = reinterpret_cast<xcb_key_press_event_t*>(event);
        pressed_keys.emplace(keypress->detail);
        released_keys.erase(keypress->detail);
        break;
      }

      case XCB_KEY_RELEASE: {
        auto keyrelease = reinterpret_cast<xcb_key_release_event_t*>(event);
        released_keys.emplace(keyrelease->detail);
        break;
      }

      case XCB_BUTTON_PRESS: {
        auto buttonpress = reinterpret_cast<xcb_button_press_event_t*>(event);
        pressed_buttons.emplace(static_cast<xcb_button_index_t>(buttonpress->detail));
        break;
      }

      case XCB_BUTTON_RELEASE: {
        auto buttonrelease = reinterpret_cast<xcb_button_release_event_t*>(event);
        released_buttons.emplace(static_cast<xcb_button_index_t>(buttonrelease->detail));
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
  TNewInit();

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

  const char* shader_watch_dirs[] = {"nyla/shipgame/shaders", "nyla/shipgame/shaders/build"};
  Vulkan_Initialize(shader_watch_dirs);

  {
    X11_KeyResolver key_resolver;
    X11_InitializeKeyResolver(key_resolver);

    game_keycodes.up = X11_ResolveKeyCode(key_resolver, "AD03");
    game_keycodes.left = X11_ResolveKeyCode(key_resolver, "AC02");
    game_keycodes.down = X11_ResolveKeyCode(key_resolver, "AC03");
    game_keycodes.right = X11_ResolveKeyCode(key_resolver, "AC04");
    game_keycodes.fire = X11_ResolveKeyCode(key_resolver, "AC07");
    game_keycodes.boost = X11_ResolveKeyCode(key_resolver, "AC08");
    game_keycodes.brake = X11_ResolveKeyCode(key_resolver, "SPCE");

    X11_FreeKeyResolver(key_resolver);
  }

  //

  RpInit(world_pipeline);
  RpInit(dbg_text_pipeline);
  RpInit(grid_pipeline);

  InitGame();

  struct Profiling {
    uint16_t xevent;
  };
  std::array<Profiling, 3> prof_data;
  uint8_t iprof = 0;

  for (;;) {
    Profiling& prof = prof_data[iprof];
    prof = {};
    iprof = (iprof + 1) % prof_data.size();

    //

    prof.xevent = GetMonotonicTimeMicros();
    ProcessXEvents();
    prof.xevent = GetMonotonicTimeMicros() - prof.xevent;

    //

    if (!running) break;

    Vulkan_FrameBegin();

    if (vk.shaders_invalidated) {
      RpInit(world_pipeline);
      RpInit(dbg_text_pipeline);
      RpInit(grid_pipeline);
      vk.shaders_invalidated = false;
    }

    if (vk.current_frame_data.dt > .05f) {
      LOG(INFO) << "dts spike: " << vk.current_frame_data.dt << "s";

      const Profiling& lastprof = prof_data[(iprof + prof_data.size() - 2) % prof_data.size()];
      LOG(INFO) << "last frame prof: xevent=" << lastprof.xevent;
    }

#if 0
    {
      Entity& line = entities[100];

      line.exists = false;
      line.affected_by_gravity = false;
      line.vertex_count = 6;

      Vulkan_CreateBuffer(1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          line.vertex_buffer, line.vertex_buffer_memory);

      vkMapMemory(vk.device, line.vertex_buffer_memory, 0, 1024, 0, &line.data);

      auto vertices = TriangulateLine({0, 0}, {100, 100}, 2.f);
      memcpy(line.data, vertices.data(), vertices.size() * sizeof(Vertex));
    }
#endif

    ProcessInput(pressed_keys, released_keys, pressed_buttons, released_buttons);

    Vulkan_RenderingBegin();
    {
      RpBegin(world_pipeline);
      RenderGameObjects();

      RpBegin(grid_pipeline);
      GridRender();

      RpBegin(dbg_text_pipeline);
      DbgText(10, 10, "fps= " + std::to_string(int(1.f / vk.current_frame_data.dt)));
    }
    Vulkan_RenderingEnd();
    Vulkan_FrameEnd();

    for (auto key : released_keys) {
      pressed_keys.erase(key);
    }
    released_keys.clear();

    for (auto button : released_buttons) {
      pressed_buttons.erase(button);
    }
    released_buttons.clear();
  }
  return 0;
}

}  // namespace nyla

int main() {
  return nyla::Main();
};