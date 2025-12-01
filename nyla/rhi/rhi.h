#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "nyla/commons/bitenum.h"
#include "nyla/commons/memory/charview.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi_handle_pool.h"

namespace nyla {

constexpr inline uint32_t rhi_max_push_constant_size = 256;
constexpr inline uint32_t rhi_max_num_frames_in_flight = 3;
constexpr inline uint32_t rhi_max_bind_group_layouts = 4;

//

struct RhiShader : RhiHandle {};
struct RhiGraphicsPipeline : RhiHandle {};
struct RhiCmdList : RhiHandle {};
struct RhiBuffer : RhiHandle {};
struct RhiBindGroup : RhiHandle {};
struct RhiBindGroupLayout : RhiHandle {};

//

enum class RhiQueueType { Graphics, Transfer };

enum class RhiBindingType { UniformBuffer, UniformBufferDynamic };

enum class RhiVertexAttributeType { Float4, Half2, SNorm8x4, UNorm8x4 };

enum class RhiCullMode { None, Back, Front };

enum class RhiFrontFace { CCW, CW };

enum class RhiInputRate { PerVertex, PerInstance };

enum class RhiFormat { Float4, Half2, SNorm8x4, UNorm8x4 };

enum class RhiShaderStage : uint32_t {
  Vertex = 1 << 0,
  Fragment = 1 << 1,
};

template <>
struct EnableBitMaskOps<RhiShaderStage> : std::true_type {};

enum class RhiBufferUsage : uint32_t {
  Vertex = 1 << 0,
  Index = 1 << 1,
  Uniform = 1 << 2,
  TransferSrc = 1 << 3,
  TransferDst = 1 << 4,
};

template <>
struct EnableBitMaskOps<RhiBufferUsage> : std::true_type {};

enum class RhiMemoryUsage { GpuOnly, CpuToGpu, GpuToCpu };

//

struct RhiShaderDesc {
  std::vector<char> code;
};

struct RhiVertexBindingDesc {
  uint32_t binding;
  uint32_t stride;
  RhiInputRate input_rate;
};

struct RhiVertexAttributeDesc {
  uint32_t location;
  uint32_t binding;
  RhiFormat format;
  uint32_t offset;
};

struct RhiBindingDesc {
  uint32_t binding;
  RhiBindingType type;
  uint32_t array_size;
  RhiShaderStage stage_flags;
};

struct RhiBufferBinding {
  RhiBuffer buffer;
  uint32_t offset;
  uint32_t size;
};

struct RhiBindGroupEntry {
  uint32_t binding;
  RhiBindingType type;
  uint32_t array_index;
  union {
    RhiBufferBinding buffer;
  };
};

struct RhiBindGroupDesc {
  RhiBindGroupLayout layout;
  RhiBindGroupEntry entries[4];
  uint32_t entries_count;
};

struct RhiBindGroupLayoutDesc {
  RhiBindingDesc bindings[16];
  uint32_t binding_count;
};

struct RhiBufferDesc {
  uint32_t size;
  RhiBufferUsage buffer_usage;
  RhiMemoryUsage memory_usage;
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

struct RhiDesc {
  PlatformWindow window;
  uint32_t num_frames_in_flight;
};

//

RhiCmdList RhiCreateCmdList(RhiQueueType queue_type);
void RhiDestroyCmdList(RhiCmdList cmd);

RhiShader RhiCreateShader(const RhiShaderDesc&);
void RhiDestroyShader(RhiShader);

RhiBuffer RhiCreateBuffer(const RhiBufferDesc&);
void RhiDestroyBuffer(RhiBuffer);

void* RhiMapBuffer(RhiBuffer, bool idempotent = true);
void RhiUnmapBuffer(RhiBuffer, bool idempotent = true);

RhiBindGroupLayout RhiCreateBindGroupLayout(const RhiBindGroupLayoutDesc&);
void RhiDestroyBindGroupLayout(RhiBindGroupLayout);

RhiBindGroup RhiCreateBindGroup(const RhiBindGroupDesc&);
void RhiDestroyBindGroup(RhiBindGroup);

RhiGraphicsPipeline RhiCreateGraphicsPipeline(const RhiGraphicsPipelineDesc&);
void RhiDestroyGraphicsPipeline(RhiGraphicsPipeline);

void RhiInit(const RhiDesc&);
uint32_t RhiGetMinUniformBufferOffsetAlignment();
uint32_t RhiGetSurfaceWidth();
uint32_t RhiGetSurfaceHeight();
uint32_t RhiGetNumFramesInFlight();
RhiCmdList RhiFrameBegin();
void RhiFrameEnd();
uint32_t RhiFrameGetIndex();
RhiCmdList RhiFrameGetCmdList();  // TODO: get rid of this

void RhiCmdBindGraphicsPipeline(RhiCmdList, RhiGraphicsPipeline);
void RhiCmdBindVertexBuffers(RhiCmdList cmd, uint32_t first_binding, std::span<const RhiBuffer> buffers,
                             std::span<const uint32_t> offsets);
void RhiCmdBindGraphicsBindGroup(RhiCmdList, uint32_t set_index, RhiBindGroup bind_group,
                                 std::span<const uint32_t> dynamic_offsets);
void RhiCmdPushGraphicsConstants(RhiCmdList cmd, uint32_t offset, RhiShaderStage stage, CharView data);
void RhiCmdDraw(RhiCmdList cmd, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex,
                uint32_t first_instance);

}  // namespace nyla