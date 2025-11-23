#pragma once

#include <cstdint>
#include <vector>

#include "nyla/apps/shipgame/world_renderer.h"
#include "nyla/commons/containers/map.h"
#include "nyla/commons/containers/set.h"
#include "nyla/commons/math/vec/vec2f.h"
#include "nyla/commons/math/vec/vec3f.h"
#include "nyla/platform/abstract_input.h"

namespace nyla {

#define NYLA_INPUT_MAPPING(X) X(Right) X(Left) X(Up) X(Down) X(Brake) X(Boost) X(Fire) X(ZoomLess) X(ZoomMore)

#define X(key) extern const AbstractInputMapping k##key;
NYLA_INPUT_MAPPING(X)
#undef X

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

  std::vector<Vertex> vertices{};
  std::span<GameObject> children{};
};
extern GameObject game_solar_system;
extern GameObject game_ship;

void ShipgameInit();
void ShipgameProcess();
void ShipgameRender();

}  // namespace nyla