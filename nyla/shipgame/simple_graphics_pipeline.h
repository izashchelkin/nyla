#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "vulkan/vulkan.h"

namespace nyla {

struct SimplePipelineUniformBuffer {
  bool enabled;
  std::vector<VkBuffer> buffer;
  std::vector<VkDeviceMemory> mem;
  std::vector<char*> mem_mapped;
  uint32_t size;
  uint32_t range;
  uint32_t written_this_frame;
};

enum class SimplePipelineVertexAttributeSlot {
  Float4,
  Half2,
  SNorm8x4,
  UNorm8x4,
};

struct SimplePipelineVertexBuffer {
  bool enabled;
  std::vector<VkBuffer> buffer;
  std::vector<VkDeviceMemory> mem;
  std::vector<char*> mem_mapped;
  uint32_t size;
  uint32_t written_this_frame;
  std::vector<SimplePipelineVertexAttributeSlot> slots;
};

struct SimplePipeline {
  VkPipeline pipeline;
  VkPipelineLayout layout;
  std::vector<VkDescriptorSet> descriptor_sets;
  std::vector<VkPipelineShaderStageCreateInfo> shader_stages;

  SimplePipelineUniformBuffer uniform;
  SimplePipelineUniformBuffer dynamic_uniform;
  SimplePipelineVertexBuffer vertex_buffer;

  void (*Init)(SimplePipeline&);
};

void SimplePipelineInit(SimplePipeline& pipeline);
void SimplePipelineBegin(SimplePipeline& pipeline);
void SimplePipelineStatic(SimplePipeline& pipeline, std::span<const char> uniform_data);
void SimplePipelineObject(SimplePipeline& pipeline, std::span<const char> vertex_data, uint32_t vertex_count,
                          std::span<const char> dynamic_uniform_data);

}  // namespace nyla