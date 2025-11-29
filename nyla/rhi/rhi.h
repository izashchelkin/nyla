#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "nyla/rhi/handle_pool.h"

namespace nyla {

constexpr uint32_t kRhiPushConstantMaxSize = 256;

enum class RhiShaderType { Vertex, Fragment };
enum class RhiQueueType { Graphics, Transfer };
enum class RhiBindingType { UniformBuffer };
enum class RhiVertexAttribute { Float4, Half2, SNorm8x4, UNorm8x4 };
enum class RhiCullMode { None, Back, Front };
enum class RhiFrontFace { CCW, CW };
enum class RhiCompareOp { LessEqual, Less, Greater, Always };
enum class RhiInputRate { PerVertex, PerInstance };
enum class RhiFormat { Float4, Half2, SNorm8x4, UNorm8x4 };

class RhiStageFlag {
  static inline uint32_t Vert = 1;
  static inline uint32_t Frag = 2;
};

//

struct RhiShader : Handle {};
struct RhiGraphicsPipeline : Handle {};

//

struct RhiShaderDesc {
  std::vector<char> code;
};

struct RhiVertexBindingDesc {
  uint32_t binding;
  uint32_t stride;
  RhiInputRate input_rate;
};

struct RhiVertexAtributeDesc {
  uint32_t location;
  uint32_t binding;
  RhiFormat format;
  uint32_t offset;
};

struct RhiBindingDesc {
  uint32_t binding;
  RhiBindingType type;
  uint32_t array_size;
  uint32_t stage_flags;
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

  RhiVertexAtributeDesc vertex_attributes[16];
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

}  // namespace nyla