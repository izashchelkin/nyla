#pragma once

#include "nyla/commons/math/vec/vec2f.h"
#include "nyla/commons/math/vec/vec3f.h"
#include "nyla/fwk/render_pipeline.h"

namespace nyla {

struct Vertex {
  Vec2f pos;
  float pad0[2] = {666.f, 666.f};

  Vertex(Vec2f pos) : pos{pos} {}
};

extern Rp world_pipeline;

void WorldSetUp();
void WorldRender(Vec2f pos, Vec3f color, float scale_x, float scale_y, const RpMesh& mesh);

}  // namespace nyla