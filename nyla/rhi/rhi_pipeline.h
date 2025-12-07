#pragma once

#include <cstdint>

#include "nyla/commons/memory/charview.h"
#include "nyla/rhi/rhi_bind_groups.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_handle.h"
#include "nyla/rhi/rhi_shader.h"
#include "nyla/rhi/rhi_texture.h"

namespace nyla
{

constexpr inline uint32_t kRhiMaxPushConstantSize = 256;

struct RhiGraphicsPipeline : RhiHandle
{
};

enum class RhiVertexFormat
{
    None,
    R32G32B32A32Float,
};

enum class RhiInputRate
{
    PerVertex,
    PerInstance
};

enum class RhiCullMode
{
    Back,
    None,
    Front
};

enum class RhiFrontFace
{
    CCW,
    CW
};

struct RhiVertexBindingDesc
{
    uint32_t binding;
    uint32_t stride;
    RhiInputRate inputRate;
};

struct RhiVertexAttributeDesc
{
    uint32_t location;
    uint32_t binding;
    RhiVertexFormat format;
    uint32_t offset;
};

struct RhiGraphicsPipelineDesc
{
    std::string debugName;

    RhiShader vertShader;
    RhiShader fragShader;

    std::array<RhiBindGroupLayout, kRhiMaxBindGroupLayouts> bindGroupLayouts;
    uint32_t bindGroupLayoutsCount;

    std::array<RhiVertexBindingDesc, 4> vertexBindings;
    uint32_t vertexBindingsCount;

    std::array<RhiVertexAttributeDesc, 16> vertexAttributes;
    uint32_t vertexAttributeCount;

    std::array<RhiTextureFormat, 4> colorTargetFormats;
    uint32_t colorTargetFormatsCount;

    RhiCullMode cullMode;
    RhiFrontFace frontFace;
};

auto RhiGetVertexFormatSize(RhiVertexFormat) -> uint32_t;

auto RhiCreateGraphicsPipeline(const RhiGraphicsPipelineDesc &) -> RhiGraphicsPipeline;
void RhiNameGraphicsPipeline(RhiGraphicsPipeline, std::string_view name);
void RhiDestroyGraphicsPipeline(RhiGraphicsPipeline);

void RhiCmdBindGraphicsPipeline(RhiCmdList, RhiGraphicsPipeline);
void RhiCmdBindVertexBuffers(RhiCmdList cmd, uint32_t firstBinding, std::span<const RhiBuffer> buffers,
                             std::span<const uint32_t> offsets);
void RhiCmdBindGraphicsBindGroup(RhiCmdList, uint32_t setIndex, RhiBindGroup bindGroup,
                                 std::span<const uint32_t> dynamicOffsets);
void RhiCmdPushGraphicsConstants(RhiCmdList cmd, uint32_t offset, RhiShaderStage stage, ByteView data);
void RhiCmdDraw(RhiCmdList cmd, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
                uint32_t firstInstance);

} // namespace nyla