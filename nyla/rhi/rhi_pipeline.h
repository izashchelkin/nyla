#pragma once

#include <cstdint>

#include "nyla/commons/memory/charview.h"
#include "nyla/rhi/rhi_bind_groups.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_handle.h"
#include "nyla/rhi/rhi_shader.h"

namespace nyla {

constexpr inline uint32_t rhi_max_push_constant_size = 256;

struct RhiGraphicsPipeline : RhiHandle {};

enum class RhiVertexFormat {
  None,

  // R32_Float,
  // R32G32_Float,
  // R32G32B32_Float,
  R32G32B32A32_Float,
  //
  // R16_Float,
  // R16G16_Float,
  // R16G16B16A16_Float,
  //
  // R32_SInt,
  // R32G32_SInt,
  // R32G32B32_SInt,
  // R32G32B32A32_SInt,
  //
  // R32_UInt,
  // R32G32_UInt,
  // R32G32B32_UInt,
  // R32G32B32A32_UInt,
  //
  // R16_SNorm,
  // R16G16_SNorm,
  // R16G16B16A16_SNorm,
  //
  // R16_UNorm,
  // R16G16_UNorm,
  // R16G16B16A16_UNorm,
  //
  // R16_SInt,
  // R16G16_SInt,
  // R16G16B16A16_SInt,
  //
  // R16_UInt,
  // R16G16_UInt,
  // R16G16B16A16_UInt,
  //
  // R8_UNorm,
  // R8G8_UNorm,
  // R8G8B8A8_UNorm,
  //
  // R8_SNorm,
  // R8G8_SNorm,
  // R8G8B8A8_SNorm,
  //
  // R8_UInt,
  // R8G8_UInt,
  // R8G8B8A8_UInt,
  //
  // R8_SInt,
  // R8G8_SInt,
  // R8G8B8A8_SInt,
  //
  // B10G11R11_UFloat,
  // R10G10B10A2_UNorm,
};

enum class RhiInputRate { PerVertex, PerInstance };

enum class RhiCullMode { None, Back, Front };

enum class RhiFrontFace { CCW, CW };

struct RhiVertexBindingDesc {
  uint32_t binding;
  uint32_t stride;
  RhiInputRate input_rate;
};

struct RhiVertexAttributeDesc {
  uint32_t location;
  uint32_t binding;
  RhiVertexFormat format;
  uint32_t offset;
};

struct RhiGraphicsPipelineDesc {
  std::string debug_name;

  RhiShader vert_shader;
  RhiShader frag_shader;

  RhiBindGroupLayout bind_group_layouts[rhi_max_bind_group_layouts];
  uint32_t bind_group_layouts_count;

  RhiVertexBindingDesc vertex_bindings[4];
  uint32_t vertex_bindings_count;

  RhiVertexAttributeDesc vertex_attributes[16];
  uint32_t vertex_attribute_count;

  RhiCullMode cull_mode;
  RhiFrontFace front_face;
};

uint32_t RhiGetVertexFormatSize(RhiVertexFormat);

RhiGraphicsPipeline RhiCreateGraphicsPipeline(const RhiGraphicsPipelineDesc&);
void RhiNameGraphicsPipeline(RhiGraphicsPipeline, std::string_view name);
void RhiDestroyGraphicsPipeline(RhiGraphicsPipeline);

void RhiCmdBindGraphicsPipeline(RhiCmdList, RhiGraphicsPipeline);
void RhiCmdBindVertexBuffers(RhiCmdList cmd, uint32_t first_binding, std::span<const RhiBuffer> buffers,
                             std::span<const uint32_t> offsets);
void RhiCmdBindGraphicsBindGroup(RhiCmdList, uint32_t set_index, RhiBindGroup bind_group,
                                 std::span<const uint32_t> dynamic_offsets);
void RhiCmdPushGraphicsConstants(RhiCmdList cmd, uint32_t offset, RhiShaderStage stage, CharView data);
void RhiCmdDraw(RhiCmdList cmd, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex,
                uint32_t first_instance);

}  // namespace nyla