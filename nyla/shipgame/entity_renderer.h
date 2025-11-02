#include "nyla/commons/math/mat4.h"
#include "nyla/commons/math/vec/vec2f.h"
#include "nyla/commons/math/vec/vec3f.h"

namespace nyla {

struct SceneUbo {
  Mat4 view;
  Mat4 proj;
};

struct EntityUbo {
  Mat4 model;
};

struct Vertex {
  Vec2f pos;
  Vec3f color;
};

void InitEntityRenderer();
void EntityRendererBefore(Vec2f camera_pos, float zoom);
void EntityRendererRecord();

}  // namespace nyla
