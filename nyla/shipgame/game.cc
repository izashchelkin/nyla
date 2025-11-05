#include "nyla/shipgame/game.h"

#include <complex>
#include <cstring>

#include "absl/log/log.h"
#include "nyla/commons/clock.h"
#include "nyla/commons/math/lerp.h"
#include "nyla/commons/math/math.h"
#include "nyla/commons/math/vec/vec2f.h"
#include "nyla/commons/types.h"
#include "nyla/shipgame/renderer.h"
#include "nyla/vulkan/vulkan.h"
#include "xcb/xproto.h"

namespace nyla {

GameKeycodes game_keycodes{};

GameObject game_solar_system{};
GameObject game_ship{};

static Vec2f game_camera_pos = {};
static float game_camera_zoom = 1.f;

static std::vector<Vertex> GenCircle(size_t n, float radius, Vec3f color) {
  CHECK(n > 4);

  std::vector<Vertex> ret;
  ret.reserve(n * 3);

  const float theta = 2.f * pi / n;
  std::complex<float> r = 1.f;

  for (size_t i = 0; i < n; ++i) {
    ret.emplace_back(Vertex{Vec2f{}, color});
    ret.emplace_back(
        Vertex{Vec2f{r.real() * radius, r.imag() * radius}, color});

    using namespace std::complex_literals;
    r *= std::cos(theta) + std::sin(theta) * 1if;

    ret.emplace_back(
        Vertex{Vec2f{r.real() * radius, r.imag() * radius}, color});
  }

  return ret;
}

static void PreRenderGameObject(GameObject& obj) {
  obj.vertices.clear();

  switch (obj.type) {
    case GameObject::Type::kSolarSystem: {
      obj.vertex_count = 0;
      break;
    }

    case GameObject::Type::kPlanet:
    case GameObject::Type::kMoon: {
      obj.vertices = GenCircle(32, 1.f, obj.color);
      obj.vertex_count = obj.vertices.size();
      break;
    }

    case GameObject::Type::kShip: {
      obj.vertex_count = 3;
      obj.vertices.reserve(3);

      obj.vertices.emplace_back(Vertex{{-0.5f, -0.36f}, obj.color});
      obj.vertices.emplace_back(Vertex{{0.5f, 0.0f}, obj.color});
      obj.vertices.emplace_back(Vertex{{-0.5f, 0.36f}, obj.color});
      break;
    }
  }

  if (obj.vertex_count) {
    memcpy(obj.vertex_data_mapped[vk.current_frame_data.iframe],
           obj.vertices.data(), sizeof(Vertex) * obj.vertex_count);

    RenderGameObject(obj);
  }

  for (GameObject& child : obj.children) {
    PreRenderGameObject(child);
  }
}

void PreRender() {
  PreRenderGameObject(game_solar_system);
  PreRenderGameObject(game_ship);
  RenderGameObjectPrepareScene(game_camera_pos, game_camera_zoom);
  RenderGameObjectFlushUniforms();
}

void InitGame() {
  {
    game_solar_system = {
        .type = GameObject::Type::kSolarSystem,
        .pos = {0.f, 0.f},
        .color = {.1f, .1f, .1f},
        .mass = 100000.f,
        .radius = 5000.f,
        .velocity = {0.f, 0.f},
    };

    auto* planets =
        new std::vector<GameObject>(3, {
                                           .type = GameObject::Type::kPlanet,
                                       });
    game_solar_system.children = {planets->data(), planets->size()};

    size_t iplanet = 0;

    {
      GameObject& planet = game_solar_system.children[iplanet++];

      planet.color = {1.f, 0.f, 0.f};

      planet.pos = {1000.f, 1000.f};
      planet.mass = 100.f;
      planet.radius = 20000.f;
      planet.orbit_radius = 2000.f;
    }

    {
      GameObject& planet = game_solar_system.children[iplanet++];
      planet.color = {0.f, 1.f, 0.f};

      planet.pos = {-1000.f, 1000.f};
      planet.mass = 50000.f;
      planet.radius = 100000.f;
      planet.orbit_radius = 10000.f;
    }

    {
      GameObject& planet = game_solar_system.children[iplanet++];
      planet.color = {0.f, 0.f, 1.f};

      planet.pos = {0, -1000.f};
      planet.mass = 50000.f;
      planet.radius = 50000.f;
      planet.orbit_radius = 30000.f;
    }
  }

  {
    game_ship = {
        .type = GameObject::Type::kShip,
        .pos = {0.f, 0.f},
        .color = {1.f, 1.f, 0.f},
        .mass = 25,
        .radius = 10,
    };
  }

  auto create_vb = [](GameObject& obj, VkDeviceSize size) {
    obj.vertex_buffer_size = size;

    for (size_t i = 0; i < kVulkan_NumFramesInFlight; ++i) {
      Vulkan_CreateBuffer(obj.vertex_buffer_size,
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          obj.vertex_buffer[i], obj.vertex_buffer_memory[i]);
      vkMapMemory(vk.device, obj.vertex_buffer_memory[i], 0,
                  obj.vertex_buffer_size, 0, &obj.vertex_data_mapped[i]);
    }
  };

  create_vb(game_ship, 1024);
  for (auto& planet : game_solar_system.children) {
    create_vb(planet, 1024);
  }
}

void ProcessInput(Set<xcb_keycode_t>& pressed_keys,
                  Set<xcb_keycode_t>& released_keys,
                  Set<xcb_button_index_t>& pressed_buttons,
                  Set<xcb_button_index_t>& released_buttons) {
#define pressed(key) pressed_keys.contains(game_keycodes.key)

  const int dx = pressed(right) - pressed(left);
  const int dy = pressed(down) - pressed(up);

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

        game_ship.angle_radians =
            LerpAngle(game_ship.angle_radians, angle, step * 5.f);
      }

      if (pressed(brake)) {
        Lerp(game_ship.velocity, Vec2f{}, step * 5.f);
      } else if (pressed(boost)) {
        const Vec2f direction =
            Vec2fNorm(Vec2f{std::cos(game_ship.angle_radians),
                            std::sin(game_ship.angle_radians)});

        Lerp(game_ship.velocity, Vec2fMul(direction, 5000.f), step);
      } else if (pressed(acceleration)) {
        const Vec2f direction =
            Vec2fNorm(Vec2f{std::cos(game_ship.angle_radians),
                            std::sin(game_ship.angle_radians)});

        Lerp(game_ship.velocity, Vec2fMul(direction, 1000.f), step * 8.f);
      } else {
        Lerp(game_ship.velocity, Vec2f{}, step);
      }

      Vec2fAdd(game_ship.pos, Vec2fMul(game_ship.velocity, step));
    }

    {
      Lerp(game_camera_pos, game_ship.pos, 5.f * step);
    }

    {
      for (GameObject& planet : game_solar_system.children) {
        using namespace std::complex_literals;

        const Vec2f v = Vec2fDif(game_solar_system.pos, planet.pos);
        const float r = Vec2fLen(v);

        const Vec2f v2 = Vec2fSum(
            Vec2fMul(
                Vec2fNorm(Vec2fApply(
                    Vec2fDif(planet.pos, game_solar_system.pos), 1.f / 1if)),
                std::max(0.f, planet.orbit_radius - r / 5.f)),
            game_solar_system.pos);

        const Vec2f vv = Vec2fDif(v2, planet.pos);

        float F =
            10000.f * 6.7f * planet.mass * game_solar_system.mass / (r * r);
        Vec2f Fv = Vec2fResized(vv, F);

        Vec2fAdd(planet.velocity, Vec2fMul(Fv, step / planet.mass));

        if (Vec2fLen(planet.velocity) > 1000.f) {
          planet.velocity = Vec2fResized(planet.velocity, 1000.f);
        }

        Vec2fAdd(planet.pos, Vec2fMul(planet.velocity, step));
      }
    }

#if 0
		{
      for (size_t i = 0; i < std::size(entities); ++i) {
        Entity& entity1 = entities[i];
        if (!entity1.exists || !entity1.affected_by_gravity || !entity1.mass)
          continue;

        Vec2f force_sum{};

        float max_f = 0;
        float max_f_orbit_radius = 0;

        for (size_t j = 0; j < std::size(entities); ++j) {
          const Entity& entity2 = entities[j];

          if (j == i) continue;
          if (!entity2.exists || !entity2.affected_by_gravity || !entity2.mass)
            continue;

          using namespace std::complex_literals;

          const Vec2f v = Vec2fDif(entity2.pos, entity1.pos);
          const float r = Vec2fLen(v);

          const Vec2f v2 = Vec2fSum(
              Vec2fMul(Vec2fNorm(Vec2fApply(Vec2fDif(entity1.pos, entity2.pos),
                                            1.f / 1if)),
                       std::max(0.f, entity2.orbit_radius - r / 5.f)),
              entity2.pos);

          const Vec2f vv = Vec2fDif(v2, entity1.pos);

#if 0
            {
              Entity& line = entities[100];
              auto vertices = TriangulateLine(entity1.pos, v2, 10.f);
              memcpy(line.data, vertices.data(),
                     vertices.size() * sizeof(Vertex));
            }
#endif

          // RenderText(
          //     200, 250,
          //     "" + std::to_string(vv[0]) + " " + std::to_string(vv[1]));

          float F = 6.7f * entity1.mass * entity2.mass / (r);
          Vec2f Fv = Vec2fResized(vv, F);

          if (F > max_f) {
            max_f = F;
            max_f_orbit_radius = entity2.orbit_radius;
          }

          Vec2fAdd(force_sum, Fv);
        }

        Vec2fAdd(entity1.velocity, Vec2fMul(force_sum, step / entity1.mass));

        if (max_f) {
          if (Vec2fLen(entity1.velocity) > 1.3 * max_f_orbit_radius) {
            entity1.velocity =
                Vec2fMul(Vec2fNorm(entity1.velocity), 1.3 * max_f_orbit_radius);
          }
        }

        Vec2fAdd(entity1.pos, Vec2fMul(entity1.velocity, step));
      }
    }
#endif
  }

  {
    if (pressed_buttons.contains(XCB_BUTTON_INDEX_5)) game_camera_zoom *= 1.1f;
    if (pressed_buttons.contains(XCB_BUTTON_INDEX_4)) game_camera_zoom /= 1.1f;

    game_camera_zoom = std::clamp(game_camera_zoom, .05f, 50.f);
  }

#undef pressed
}

}  // namespace nyla
