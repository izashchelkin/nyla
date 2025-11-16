#pragma once

#include "nyla/commons/math/vec/vec2f.h"
#include "nyla/commons/math/vec/vec3f.h"
#include "nyla/vulkan/render_pipeline.h"

namespace nyla {

struct WorldRendererVertex {
  Vec2f pos;
  float pad0[2] = {666.f, 666.f};
  Vec3f color;
  float pad1[1] = {777.f};

  WorldRendererVertex(Vec2f pos, Vec3f color) : pos{pos}, color{color} {}
};

extern RenderPipeline world_pipeline;
extern RenderPipeline grid_pipeline;

void WorldSetUp(Vec2f game_camera_pos, float game_camera_zoom);
void WorldRender(Vec2f pos, float angle_radians, std::span<WorldRendererVertex> vertices);
void GridRender();

}  // namespace nyla