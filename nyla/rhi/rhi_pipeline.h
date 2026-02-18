#pragma once

#include <cstdint>
#include <string>

#include "nyla/commons/containers/inline_string.h"
#include "nyla/commons/handle.h"
#include "nyla/rhi/rhi_shader.h"
#include "nyla/rhi/rhi_texture.h"

namespace nyla
{

struct RhiGraphicsPipeline : Handle
{
};

enum class RhiVertexFormat
{
    None,
    R32G32B32A32Float,
    R32G32B32Float,
    R32G32Float,
};

enum class RhiInputRate
{
    PerVertex,
    PerInstance
};

enum class RhiCullMode
{
    None,
    Back,
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
    uint32_t binding;
    InlineString<16> semantic;
    RhiVertexFormat format;
    uint32_t offset;
};

struct RhiGraphicsPipelineDesc
{
    std::string debugName;

    RhiShader vs;
    RhiShader ps;

    std::span<RhiVertexBindingDesc> vertexBindings;
    std::span<RhiVertexAttributeDesc> vertexAttributes;
    std::span<RhiTextureFormat> colorTargetFormats;
    RhiTextureFormat depthFormat;

    RhiCullMode cullMode;
    RhiFrontFace frontFace;
};

} // namespace nyla