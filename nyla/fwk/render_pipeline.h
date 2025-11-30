#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "nyla/commons/memory/charview.h"
#include "nyla/rhi/rhi.h"

namespace nyla {

struct RpBuf {
  bool enabled;
  uint32_t stage_flags;
  uint32_t size;
  uint32_t range;
  uint32_t written;
  std::vector<RhiVertexAttributeType> attrs;
  RhiBuffer buffer[RhiDesc::kMaxFramesInFlight];
  std::vector<char*> mem_mapped;
};

inline VkShaderStageFlags RpBufStageFlags(const RpBuf& buf) {
  if (buf.stage_flags)
    return buf.stage_flags;
  else
    return VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
}

struct Rp {
  std::string name;

  RhiGraphicsPipeline pipeline;

  std::vector<VkDescriptorSet> desc_sets;

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

}  // namespace nyla