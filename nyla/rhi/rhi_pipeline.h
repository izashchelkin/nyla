#pragma once

#include <array>
#include <cstdint>
#include <string>

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

enum class RhiVertexSemantic
{
    Position,
    Color,
    TexCoord,
};

struct RhiVertexAttributeDesc
{
    uint32_t binding;
    RhiVertexFormat format;
    uint32_t offset;
};

struct RhiGraphicsPipelineDesc
{
    std::string debugName;

    RhiShader vs;
    RhiShader ps;

    uint32_t vertexBindingsCount;
    std::array<RhiVertexBindingDesc, 4> vertexBindings;

    uint32_t vertexAttributeCount;
    std::array<RhiVertexAttributeDesc, 16> vertexAttributes;

    uint32_t colorTargetFormatsCount;
    std::array<RhiTextureFormat, 4> colorTargetFormats;

    RhiCullMode cullMode;
    RhiFrontFace frontFace;
};

} // namespace nyla