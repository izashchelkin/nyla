#pragma once

#include "nyla/commons/math/vec/vec2f.h"
#include "nyla/commons/math/vec/vec3f.h"
#include "nyla/vulkan/render_pipeline.h"

namespace nyla {

struct Vertex {
  Vec2f pos;
  float pad0[2] = {666.f, 666.f};
  Vec3f color;
  float pad1[1] = {777.f};

  Vertex(Vec2f pos, Vec3f color) : pos{pos}, color{color} {}
};

extern Rp world_pipeline;

void WorldSetUp();
void WorldRender(Vec2f pos, std::span<Vertex> vertices);

}  // namespace nyla