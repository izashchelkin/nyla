#include <vector>

#include "nyla/commons/math/vec/vec2f.h"
#include "nyla/commons/math/vec/vec3f.h"
#include "vulkan/vulkan.h"

namespace nyla {

struct SimplePipelineVertex {
  Vec2f pos;
  Vec3f color;
};

struct SimplePipelineUniform {
  std::vector<VkBuffer> buffer;
  VkDeviceSize size;
  VkDeviceSize range;
  std::vector<VkDeviceMemory> mem;
  std::vector<void*> mem_mapped;
};

struct SimplePipeline {
  VkPipeline pipeline;
  VkPipelineLayout layout;
  std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
  std::vector<VkDescriptorSet> descriptor_sets;

  bool has_uniform;
  SimplePipelineUniform uniform;
  bool has_dynamic_uniform;
  SimplePipelineUniform dynamic_uniform;

  bool has_vertex_input;
  std::vector<VkBuffer> vertex_buffers;

  void (*Init)(SimplePipeline&);
};

}  // namespace nyla
