#include "nyla/commons/math/mat4.h"

namespace nyla {

struct SceneUbo {
  Mat4 view;
  Mat4 proj;
};

struct EntityUbo {
  Mat4 model;
};

struct Vertex {
  Vec2 pos;
  Vec3 color;
};

void InitEntityRenderer();
void EntityRendererBefore(Vec2 camera_pos, float zoom);
void EntityRendererRecord();

}  // namespace nyla
