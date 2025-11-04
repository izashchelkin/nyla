#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/strings/str_join.h"
#include "nyla/commons/clock.h"
#include "nyla/commons/logging/init.h"
#include "nyla/commons/math/lerp.h"
#include "nyla/commons/math/mat4.h"
#include "nyla/commons/math/math.h"
#include "nyla/commons/math/vec/vec2f.h"
#include "nyla/commons/math/vec/vec3f.h"
#include "nyla/commons/memory/bump_alloc.h"
#include "nyla/commons/readfile.h"
#include "nyla/commons/types.h"
#include "nyla/shipgame/circle.h"
#include "nyla/shipgame/entity_renderer.h"
#include "nyla/shipgame/gamecommon.h"
#include "nyla/shipgame/text_renderer.h"
#include "nyla/vulkan/vulkan.h"
#include "nyla/x11/x11.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

uint16_t ientity;
Entity entities[256];

static bool running = true;
static xcb_window_t window;

static Set<xcb_keycode_t> pressed_keys;
static Set<xcb_keycode_t> released_keys;
static Set<xcb_button_index_t> pressed_buttons;
static Set<xcb_button_index_t> released_buttons;

VkExtent2D Vulkan_PlatformGetWindowExtent() {
  xcb_get_geometry_reply_t* window_geometry = xcb_get_geometry_reply(
      x11.conn, xcb_get_geometry(x11.conn, window), nullptr);

  return {.width = window_geometry->width, .height = window_geometry->height};
}

void Vulkan_PlatformSetSurface() {
  const VkXcbSurfaceCreateInfoKHR surface_create_info{
      .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
      .connection = x11.conn,
      .window = window,
  };
  VK_CHECK(vkCreateXcbSurfaceKHR(vk.instance, &surface_create_info, nullptr,
                                 &vk.surface));
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
        pressed_buttons.emplace(
            static_cast<xcb_button_index_t>(buttonpress->detail));
        break;
      }

      case XCB_BUTTON_RELEASE: {
        auto buttonrelease =
            reinterpret_cast<xcb_button_release_event_t*>(event);
        released_buttons.emplace(
            static_cast<xcb_button_index_t>(buttonrelease->detail));
        break;
      }

      case XCB_CLIENT_MESSAGE: {
        auto clientmessage =
            reinterpret_cast<xcb_client_message_event_t*>(event);

        if (clientmessage->format == 32 &&
            clientmessage->type == x11.atoms.wm_protocols &&
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
  InitGlobalTnew();

  X11_Initialize();

  {
    window = xcb_generate_id(x11.conn);

    CHECK(!xcb_request_check(
        x11.conn,
        xcb_create_window_checked(
            x11.conn, XCB_COPY_FROM_PARENT, window, x11.screen->root, 0, 0,
            x11.screen->width_in_pixels, x11.screen->height_in_pixels, 0,
            XCB_WINDOW_CLASS_INPUT_OUTPUT, x11.screen->root_visual,
            XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK,
            (uint32_t[]){false, XCB_EVENT_MASK_KEY_PRESS |
                                    XCB_EVENT_MASK_KEY_RELEASE |
                                    XCB_EVENT_MASK_BUTTON_PRESS |
                                    XCB_EVENT_MASK_BUTTON_RELEASE})));

    xcb_map_window(x11.conn, window);
    xcb_flush(x11.conn);
  }

  Vulkan_Initialize();

  xcb_keycode_t up_keycode;
  xcb_keycode_t left_keycode;
  xcb_keycode_t down_keycode;
  xcb_keycode_t right_keycode;
  xcb_keycode_t acceleration_keycode;
  xcb_keycode_t boost_keycode;
  xcb_keycode_t brake_keycode;
  xcb_keycode_t fire_keycode;

  {
    X11_KeyResolver key_resolver;
    X11_InitializeKeyResolver(key_resolver);

    up_keycode = X11_ResolveKeyCode(key_resolver, "AD03");
    left_keycode = X11_ResolveKeyCode(key_resolver, "AC02");
    down_keycode = X11_ResolveKeyCode(key_resolver, "AC03");
    right_keycode = X11_ResolveKeyCode(key_resolver, "AC04");
    acceleration_keycode = X11_ResolveKeyCode(key_resolver, "AC07");
    boost_keycode = X11_ResolveKeyCode(key_resolver, "AC08");
    brake_keycode = X11_ResolveKeyCode(key_resolver, "SPCE");
    fire_keycode = X11_ResolveKeyCode(key_resolver, "AC08");

    X11_FreeKeyResolver(key_resolver);
  }

  //

  InitEntityRenderer();
  InitTextRenderer();

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

    if (vk.current_frame_data.dt > .05f) {
      LOG(INFO) << "dts spike: " << vk.current_frame_data.dt << "s";

      const Profiling& lastprof =
          prof_data[(iprof + prof_data.size() - 2) % prof_data.size()];
      LOG(INFO) << "last frame prof: xevent=" << lastprof.xevent;
    }

    static Entity& ship = *[] {
      Entity& ship = entities[ientity++];

      VkBuffer ship_vertex_buffer;
      VkDeviceMemory ship_vertex_buffer_memory;
      const std::vector<Vertex> ship_vertices = {
          {{-25.f, -18.f}, {1.0f, 0.0f, 0.3f}},
          {{25.f, 0.f}, {1.0f, 0.0f, 0.0f}},
          {{-25.f, 18.f}, {1.0f, 0.3f, 0.0f}},
      };

      Vulkan_CreateBuffer(vk.command_pool, vk.queue,
                          sizeof(ship_vertices[0]) * ship_vertices.size(),
                          ship_vertices.data(),
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, ship_vertex_buffer,
                          ship_vertex_buffer_memory);

      ship.exists = true;
      ship.affected_by_gravity = false;
      ship.mass = 0;
      ship.density = 0;
      ship.vertex_count = ship_vertices.size();
      ship.vertex_buffer = ship_vertex_buffer;
      ship.vertex_buffer_memory = ship_vertex_buffer_memory;

      return &ship;
    }();

    static std::span<Entity> asteroids = [] {
      //

      std::span<Entity> asteroids = {&entities[ientity], 64};
      ientity += asteroids.size();

      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_real_distribution<float> dist(-x11.screen->width_in_pixels,
                                                 x11.screen->width_in_pixels);

      for (size_t i = 0; i < std::size(asteroids); ++i) {
        auto& asteroid = asteroids[i];

        asteroid.density = 0.0001;

        switch (i % 2) {
          case 0:
            asteroid.mass = 300;
            break;
          case 1:
            asteroid.mass = 50;
            break;
        }
        if (i == 0) asteroid.mass = 300000;

        float volume = asteroid.mass / asteroid.density;
        float radius = std::cbrt(volume * 3.f / 4.f / pi);

        std::vector<Vertex> asteroid_vertices;
        {
          std::vector<Vec2f> asteroid_vertex_positions =
              TriangulateCircle(10, radius);
          asteroid_vertices.reserve(asteroid_vertex_positions.size());
          for (Vec2f& pos : asteroid_vertex_positions) {
            asteroid_vertices.emplace_back(Vertex{pos, {.3f, 1.f, .3f}});
          }
        }

        VkBuffer asteroid_vertex_buffer;
        VkDeviceMemory asteroid_vertex_buffer_memory;
        Vulkan_CreateBuffer(
            vk.command_pool, vk.queue,
            sizeof(Vertex) * asteroid_vertices.size(), asteroid_vertices.data(),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, asteroid_vertex_buffer,
            asteroid_vertex_buffer_memory);

        asteroid.exists = true;
        asteroid.affected_by_gravity = true;
        asteroid.orbit_radius = radius + 10;

        asteroid.pos[0] = dist(gen);
        asteroid.pos[1] = dist(gen);

        asteroid.vertex_count = asteroid_vertices.size();
        asteroid.vertex_buffer = asteroid_vertex_buffer;
        asteroid.vertex_buffer_memory = asteroid_vertex_buffer_memory;
      }

      return asteroids;
    }();

    static Vec2f camera_pos = {};

    const int dx = (pressed_keys.contains(right_keycode) ? 1 : 0) -
                   (pressed_keys.contains(left_keycode) ? 1 : 0);
    const int dy = (pressed_keys.contains(down_keycode) ? 1 : 0) -
                   (pressed_keys.contains(up_keycode) ? 1 : 0);

    static float dt_accumulator = 0.f;
    dt_accumulator += vk.current_frame_data.dt;

    constexpr float step = 1.f / 120.f;
    for (; dt_accumulator >= step; dt_accumulator -= step) {
      {
        if (dx || dy) {
          float angle =
              std::atan2(-static_cast<float>(dy), static_cast<float>(dx));
          if (angle < 0.f) {
            angle += 2.f * pi;
          }

          ship.angle_radians = LerpAngle(ship.angle_radians, angle, step * 5.f);
        }

        if (pressed_keys.contains(brake_keycode)) {
          Lerp(ship.velocity, Vec2f{}, step * 5.f);
        } else if (pressed_keys.contains(boost_keycode)) {
          const Vec2f direction = Vec2fNorm(Vec2f{
              std::cos(ship.angle_radians), std::sin(ship.angle_radians)});

          Lerp(ship.velocity, Vec2fMul(direction, 5000.f), step);
        } else if (pressed_keys.contains(acceleration_keycode)) {
          const Vec2f direction = Vec2fNorm(Vec2f{
              std::cos(ship.angle_radians), std::sin(ship.angle_radians)});

          Lerp(ship.velocity, Vec2fMul(direction, 1000.f), step * 8.f);
        } else {
          Lerp(ship.velocity, Vec2f{}, step);
        }

        Vec2fAdd(ship.pos, *Tnew<Vec2f>(Vec2fMul(ship.velocity, step)));
      }

      {
        Lerp(camera_pos, ship.pos, 5.f * step);
      }

      {
        for (size_t i = 0; i < std::size(entities); ++i) {
          Entity& entity1 = entities[i];
          if (!entity1.exists || !entity1.affected_by_gravity || !entity1.mass)
            continue;

          Vec2f force_sum{};

          for (size_t j = 0; j < std::size(entities); ++j) {
            const Entity& entity2 = entities[j];

            if (j == i) continue;
            if (!entity2.exists || !entity2.affected_by_gravity ||
                !entity2.mass)
              continue;

            Vec2f v = Vec2fDif(entity2.pos, entity1.pos);
            const float r = Vec2fLen(v);

            using namespace std::complex_literals;

            if (entity2.orbit_radius && r < 10 + entity2.orbit_radius &&
                entity2.mass - entity1.mass >= 100) {
              Vec2f vv = Vec2fApply(v, 1if);
            }

            float F = 100 * 6.7f * entity1.mass * entities[j].mass / (r * r);

            Vec2fAdd(force_sum, *Tnew<Vec2f>(Vec2fMul(v, F / Vec2fLen(v))));
          }

          Vec2fAdd(entity1.velocity, Vec2fMul(force_sum, step / entity1.mass));
          Vec2fAdd(entity1.pos, Vec2fMul(entity1.velocity, step));
        }
      }
    }

    static float zoom = 1.f;

    {
      if (pressed_buttons.contains(XCB_BUTTON_INDEX_5)) zoom += .5f;
      if (pressed_buttons.contains(XCB_BUTTON_INDEX_4)) zoom -= .5f;

      zoom = std::clamp(zoom, .1f, 10.f);
    }

    RenderText(200, 200, "zoom: " + std::to_string(zoom));
    RenderText(200, 250,
               "dt: " + std::to_string(1.f / vk.current_frame_data.dt));

    EntityRendererBefore(camera_pos, zoom);
    TextRendererBefore();

    Vulkan_RenderingBegin();
    {
      EntityRendererRecord();
      TextRendererRecord();
    }
    Vulkan_RenderingEnd();
    Vulkan_FrameEnd();

    BumpAllocReset();

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

int main() { return nyla::Main(); };
