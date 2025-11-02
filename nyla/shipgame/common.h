#pragma once

#include <cstdint>

#include "nyla/commons/math/vec/vec2f.h"
#include "nyla/vulkan/vulkan.h"

namespace nyla {

struct Entity {
  uint32_t flags;
  Vec2f pos;
  Vec2f velocity;
  float mass;
  float density;
  float angle_radians;

  Vec2f hit_rect;

  uint32_t vertex_count;
  uint32_t vertex_offset;
  VkBuffer vertex_buffer;
  VkDeviceMemory vertex_buffer_memory;
};

extern uint16_t ientity;
extern Entity entities[256];

}  // namespace nyla
