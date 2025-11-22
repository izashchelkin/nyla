#include "nyla/apps/breakout/breakout.h"

#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>

#include "nyla/apps/breakout/world_renderer.h"
#include "nyla/commons/containers/set.h"
#include "nyla/commons/math/lerp.h"
#include "nyla/commons/math/math.h"
#include "nyla/commons/math/vec/vec2f.h"
#include "nyla/commons/math/vec/vec4f.h"
#include "nyla/commons/os/clock.h"
#include "nyla/fwk/input.h"
#include "nyla/vulkan/vulkan.h"

namespace nyla {

#define X(key) const InputMappingId k##key;
NYLA_INPUT_MAPPING(X)
#undef X

static float pos_x = 0.f;

void ProcessInput() {
  const int dx = Pressed(kRight) - Pressed(kLeft);

  static float dt_accumulator = 0.f;
  dt_accumulator += vk.current_frame_data.dt;

  constexpr float step = 1.f / 120.f;
  for (; dt_accumulator >= step; dt_accumulator -= step) {
    pos_x += 100.f * step * dx;
  }
}

void BreakoutInit() {}

void BreakoutRender() {
  {
    const float width = 3.f;
    const float height = .8f;

    std::vector<Vertex> vertices;

    vertices.emplace_back(Vertex{Vec2f{0, 0}, Vec3f{1.f, 1.f, 1.f}});
    vertices.emplace_back(Vertex{Vec2f{width, 0 - height}, Vec3f{1.f, 1.f, 1.f}});
    vertices.emplace_back(Vertex{Vec2f{width, 0}, Vec3f{1.f, 1.f, 1.f}});

    vertices.emplace_back(Vertex{Vec2f{0, 0}, Vec3f{1.f, 1.f, 1.f}});
    vertices.emplace_back(Vertex{Vec2f{0, 0 - height}, Vec3f{1.f, 1.f, 1.f}});
    vertices.emplace_back(Vertex{Vec2f{width, 0 - height}, Vec3f{1.f, 1.f, 1.f}});

    WorldRender(Vec2f{pos_x, -30.f}, vertices);
  }
}

}  // namespace nyla