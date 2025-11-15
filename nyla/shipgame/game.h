#pragma once

#include <cstdint>
#include <vector>

#include "nyla/commons/containers/set.h"
#include "nyla/commons/math/mat4.h"
#include "nyla/commons/math/vec/vec2f.h"
#include "nyla/commons/math/vec/vec3f.h"
#include "nyla/vulkan/vulkan.h"

namespace nyla {

struct Vertex {
  Vec2f pos;
  float pad_a[2] = {666.f, 666.f};
  Vec3f color;
  float pad_b[1] = {777.f};

  Vertex(Vec2f pos, Vec3f color) : pos{pos}, color{color} {}
};

struct GameObject {
  enum class Type : uint8_t {
    kSolarSystem,
    kPlanet,
    kMoon,
    kShip,
  };

  Type type;
  Vec2f pos{};
  Vec3f color{};
  float angle_radians;
  float mass;
  float radius;
  float orbit_radius;
  Vec2f velocity{};

  std::vector<Vertex> vertices{};
  std::span<GameObject> children{};
};
extern GameObject game_solar_system;
extern GameObject game_ship;

extern struct GameKeycodes {
  xcb_keycode_t up;
  xcb_keycode_t left;
  xcb_keycode_t down;
  xcb_keycode_t right;
  xcb_keycode_t acceleration;
  xcb_keycode_t boost;
  xcb_keycode_t brake;
  xcb_keycode_t fire;
} game_keycodes;

struct SceneUbo {
  Mat4 view;
  Mat4 proj;
};

struct GameObjectUbo {
  Mat4 model;
};

void InitGame();
void ProcessInput(Set<xcb_keycode_t>& pressed_keys, Set<xcb_keycode_t>& released_keys,
                  Set<xcb_button_index_t>& pressed_buttons, Set<xcb_button_index_t>& released_buttons);
void RenderGameObjects();

}  // namespace nyla