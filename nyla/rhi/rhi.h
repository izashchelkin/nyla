#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "nyla/rhi/handle_pool.h"

namespace nyla {

constexpr uint32_t kRhiPushConstantMaxSize = 256;

enum class RhiShaderType : uint32_t {
  Vertex,
  Fragment,
};

enum class RhiQueueType : uint32_t {
  Graphics,
  Transfer,
};

enum class RhiBindingType : uint32_t {
  UniformBuffer,
};

enum class RhiVertexAttributeType : uint32_t {
  Float4,
  Half2,
  SNorm8x4,
  UNorm8x4,
};

enum class RhiCullMode : uint32_t {
  None,
  Back,
  Front,
};

enum class RhiFrontFace : uint32_t {
  CCW,
  CW,
};

enum class RhiInputRate : uint32_t {
  PerVertex,
  PerInstance,
};

enum class RhiFormat : uint32_t {
  Float4,
  Half2,
  SNorm8x4,
  UNorm8x4,
};

enum class RhiShaderStage : uint32_t {
  None = 0,
  Vertex = 1 << 0,
  Fragment = 1 << 1,
};

inline RhiShaderStage operator|(RhiShaderStage a, RhiShaderStage b) {
  return static_cast<RhiShaderStage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline uint32_t operator&(RhiShaderStage a, RhiShaderStage b) {
  return static_cast<uint32_t>(a) & static_cast<uint32_t>(b);
}

//

struct RhiShader : Handle {};
struct RhiGraphicsPipeline : Handle {};
struct RhiCmdList : Handle {};
struct RhiBuffer : Handle {};

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

struct RhiBindGroupLayout {
  RhiBindingDesc bindings[16];
  uint32_t binding_count;
};

struct RhiGraphicsPipelineDesc {
  std::string debug_name;

  RhiShader vert_shader;
  RhiShader frag_shader;

  RhiBindGroupLayout bind_layouts[16];
  uint32_t bind_layout_count;

  RhiVertexBindingDesc vertex_bindings[4];
  uint32_t vertex_bindings_count;

  RhiVertexAttributeDesc vertex_attributes[16];
  uint32_t vertex_attribute_count;

  RhiCullMode cull_mode;
  RhiFrontFace front_face;
  RhiCompareOp compare_op;
};

struct RhiDesc {
  constexpr static uint32_t kMaxFramesInFlight = 3;

  uint32_t num_frames_in_flight;
};

void RhiInit(RhiDesc);
RhiShader RhiCreateShader(RhiShaderDesc);
RhiGraphicsPipeline RhiCreateGraphicsPipeline(RhiGraphicsPipelineDesc);
RhiCmdList RhiFrameBegin();
void RhiFrameEnd();

}  // namespace nyla