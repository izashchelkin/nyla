#include <cstdint>

#include "nyla/commons/math/vec2.h"
#include "nyla/vulkan/vulkan.h"

namespace nyla {

struct Entity {
  uint32_t flags;
  Vec2 pos;
  Vec2 velocity;
  float mass;
  float density;
  float angle_radians;

  Vec2 hit_rect;

  uint32_t vertex_count;
  uint32_t vertex_offset;
  VkBuffer vertex_buffer;
  VkDeviceMemory vertex_buffer_memory;
};

extern uint16_t ientity;
extern Entity entities[256];

}  // namespace nyla
