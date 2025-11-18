#pragma once

#include <cstdint>
#include <vector>

#include "nyla/apps/shipgame/world_renderer.h"
#include "nyla/commons/containers/map.h"
#include "nyla/commons/containers/set.h"
#include "nyla/commons/math/mat4.h"
#include "nyla/commons/math/vec/vec2f.h"
#include "nyla/commons/math/vec/vec3f.h"
#include "nyla/fwk/input.h"
#include "nyla/vulkan/vulkan.h"

namespace nyla {

extern const InputMappingId kRight;
extern const InputMappingId kLeft;
extern const InputMappingId kUp;
extern const InputMappingId kDown;
extern const InputMappingId kBrake;
extern const InputMappingId kBoost;
extern const InputMappingId kFire;
extern const InputMappingId kZoomLess;
extern const InputMappingId kZoomMore;

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
  float scale;
  float orbit_radius;
  Vec2f velocity{};

  std::vector<WorldRendererVertex> vertices{};
  std::span<GameObject> children{};
};
extern GameObject game_solar_system;
extern GameObject game_ship;

struct SceneUbo {
  Mat4 view;
  Mat4 proj;
};

struct GameObjectUbo {
  Mat4 model;
};

void InitGame();
void ProcessInput();
void RenderGameObjects();

}  // namespace nyla