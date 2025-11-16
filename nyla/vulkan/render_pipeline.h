#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "vulkan/vulkan.h"

namespace nyla {

struct RpUniformBuffer {
  bool enabled;
  std::vector<VkBuffer> buffer;
  std::vector<VkDeviceMemory> mem;
  std::vector<char*> mem_mapped;
  uint32_t size;
  uint32_t range;
  uint32_t written_this_frame;
};

enum class RpVertexAttr {
  Float4,
  Half2,
  SNorm8x4,
  UNorm8x4,
};

VkFormat RpVertexAttrVkFormat(RpVertexAttr attr);
uint32_t RpVertexAttrSize(RpVertexAttr attr);

struct RpVertexBuf {
  bool enabled;
  std::vector<VkBuffer> buffer;
  std::vector<VkDeviceMemory> mem;
  std::vector<char*> mem_mapped;
  uint32_t size;
  uint32_t written_this_frame;
  std::vector<RpVertexAttr> attrs;
};

struct RenderPipeline {
  VkPipeline pipeline;
  VkPipelineLayout layout;
  std::vector<VkDescriptorSet> descriptor_sets;
  std::vector<VkPipelineShaderStageCreateInfo> shader_stages;

  RpUniformBuffer uniform;
  RpUniformBuffer dynamic_uniform;
  RpVertexBuf vertex_buffer;

  void (*Init)(RenderPipeline&);
};

void RpInit(RenderPipeline& rp);
void RpBegin(RenderPipeline& rp);
void RpSetStaticUniform(RenderPipeline& rp, std::span<const char> uniform_data);
void RpDraw(RenderPipeline& rp, uint32_t vertex_count, std::span<const char> vertex_data,
            std::span<const char> dynamic_uniform_data);

}  // namespace nyla