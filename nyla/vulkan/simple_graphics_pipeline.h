#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "vulkan/vulkan.h"

namespace nyla {

struct SgpUniformBuffer {
  bool enabled;
  std::vector<VkBuffer> buffer;
  std::vector<VkDeviceMemory> mem;
  std::vector<char*> mem_mapped;
  uint32_t size;
  uint32_t range;
  uint32_t written_this_frame;
};

enum class SgpVertexAttr {
  Float4,
  Half2,
  SNorm8x4,
  UNorm8x4,
};

VkFormat SgpVertexAttrVkFormat(SgpVertexAttr attr);
uint32_t SgpVertexAttrSize(SgpVertexAttr attr);

struct SgpVertexBuf {
  bool enabled;
  std::vector<VkBuffer> buffer;
  std::vector<VkDeviceMemory> mem;
  std::vector<char*> mem_mapped;
  uint32_t size;
  uint32_t written_this_frame;
  std::vector<SgpVertexAttr> attrs;
};

struct Sgp {
  VkPipeline pipeline;
  VkPipelineLayout layout;
  std::vector<VkDescriptorSet> descriptor_sets;
  std::vector<VkPipelineShaderStageCreateInfo> shader_stages;

  SgpUniformBuffer uniform;
  SgpUniformBuffer dynamic_uniform;
  SgpVertexBuf vertex_buffer;

  void (*Init)(Sgp&);
};

void SgpInit(Sgp& pipeline);
void SgpBegin(Sgp& pipeline);
void SgpStatic(Sgp& pipeline, std::span<const char> uniform_data);
void SgpObject(Sgp& pipeline, std::span<const char> vertex_data, uint32_t vertex_count,
               std::span<const char> dynamic_uniform_data);

}  // namespace nyla