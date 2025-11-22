#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "nyla/commons/memory/charview.h"
#include "nyla/vulkan/vulkan.h"

namespace nyla {

constexpr uint32_t kPushConstantMaxSize = 128;

enum class RpVertAttr {
  Float4,
  Half2,
  SNorm8x4,
  UNorm8x4,
};

struct RpBuf {
  bool enabled;
  uint32_t size;
  uint32_t range;
  uint32_t written;
  std::vector<RpVertAttr> attrs;
  std::vector<VkBuffer> buffer;
  std::vector<VkDeviceMemory> mem;
  std::vector<char*> mem_mapped;
};

struct Rp {
  std::string_view name;

  VkPipeline pipeline;
  VkPipelineLayout layout;
  std::vector<VkDescriptorSet> desc_sets;
  std::vector<VkPipelineShaderStageCreateInfo> shader_stages;

  bool disable_culling;
  RpBuf static_uniform;
  RpBuf dynamic_uniform;
  RpBuf vert_buf;

  void (*Init)(Rp&);
};

struct RpMesh {
  VkDeviceSize offset;
  uint32_t vert_count;
};

void RpInit(Rp& rp);
void RpAttachVertShader(Rp& rp, const std::string& path);
void RpAttachFragShader(Rp& rp, const std::string& path);
void RpBegin(Rp& rp);
void RpPushConst(Rp& rp, CharView data);
void RpStaticUniformCopy(Rp& rp, CharView data);
RpMesh RpVertCopy(Rp& rp, uint32_t vert_count, CharView vert_data);
void RpDraw(Rp& rp, RpMesh mesh, CharView dynamic_uniform_data);

VkFormat RpVertAttrVkFormat(RpVertAttr attr);
uint32_t RpVertAttrSize(RpVertAttr attr);

}  // namespace nyla