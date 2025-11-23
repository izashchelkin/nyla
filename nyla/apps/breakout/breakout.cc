#include "nyla/apps/breakout/breakout.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <vector>

#include "nyla/apps/breakout/unitshapes.h"
#include "nyla/apps/breakout/world_renderer.h"
#include "nyla/commons/color.h"
#include "nyla/commons/containers/set.h"
#include "nyla/commons/math/lerp.h"
#include "nyla/commons/math/math.h"
#include "nyla/commons/math/vec/vec2f.h"
#include "nyla/commons/math/vec/vec3f.h"
#include "nyla/commons/math/vec/vec4f.h"
#include "nyla/commons/memory/charview.h"
#include "nyla/commons/os/clock.h"
#include "nyla/fwk/input.h"
#include "nyla/vulkan/render_pipeline.h"
#include "nyla/vulkan/vulkan.h"

namespace nyla {

#define X(key) const InputMappingId k##key;
NYLA_INPUT_MAPPING(X)
#undef X

static float pos_x = 0.f;

namespace {

struct Brick {
  float x;
  float y;
  float width;
  float height;
  Vec3f color;
};

struct Level {
  std ::vector<Brick> bricks;
};

}  // namespace

void ProcessInput() {
  const int dx = Pressed(kRight) - Pressed(kLeft);

  static float dt_accumulator = 0.f;
  dt_accumulator += vk.current_frame_data.dt;

  constexpr float step = 1.f / 120.f;
  for (; dt_accumulator >= step; dt_accumulator -= step) {
    pos_x += 100.f * step * dx;
    pos_x = std::clamp(pos_x, -30.f, 30.f);
  }
}

static Level level;

void BreakoutInit() {
  for (size_t i = 0; i < 12; ++i) {
    float h = std::fmod(static_cast<float>(i) + 825.f, 12.f) / 12.f;
    float s = .97f;
    float v = .97f;

    Vec3f color = ConvertHsvToRgb(h, s, v);

    for (size_t j = 0; j < 17; ++j) {
      level.bricks.emplace_back(Brick{
          -28.f + j * 3.5f,
          20.f - i * 1.5f,
          3.f,
          1.f,
          color,
      });
    }
  }
}

enum class GameStage {
  kStartMenu,
  kGame,
};
static GameStage stage = GameStage::kGame;

void BreakoutRender() {
  static RpMesh unit_rect_mesh = [] {
    std::vector<Vertex> unit_rect;
    unit_rect.reserve(6);
    GenUnitRect([&unit_rect](float x, float y) { unit_rect.emplace_back(Vertex{Vec2f{x, y}}); });
    return RpVertCopy(world_pipeline, unit_rect.size(), CharViewSpan(std::span{unit_rect}));
  }();

  static RpMesh unit_circle_mesh = [] {
    std::vector<Vertex> unit_circle;
    unit_circle.reserve(32 * 3);
    GenUnitCircle(32, [&unit_circle](float x, float y) { unit_circle.emplace_back(Vertex{Vec2f{x, y}}); });
    return RpVertCopy(world_pipeline, unit_circle.size(), CharViewSpan(std::span{unit_circle}));
  }();

  if (stage == GameStage::kGame) {
    for (Brick& brick : level.bricks) {
      WorldRender(Vec2f{brick.x, brick.y}, brick.color, brick.width, brick.height, unit_rect_mesh);
    }

    WorldRender(Vec2f{pos_x, -30.f}, Vec3f{.1f, .1f, .99f}, 3.f, .8f, unit_rect_mesh);

    WorldRender(Vec2f{0, 0}, Vec3f{.5f, 0.f, 1.f}, .8f, .8f, unit_circle_mesh);
  }
}

}  // namespace nyla