#pragma once

#include "nyla/commons/handle.h"
#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_sampler.h"
#include "nyla/rhi/rhi_shader.h"
#include "nyla/rhi/rhi_texture.h"
#include <cstdint>

namespace nyla
{

constexpr inline uint32_t kRhiMaxBindGroupLayouts = 4;

struct RhiBindGroup : Handle
{
};
struct RhiBindGroupLayout : Handle
{
};

enum class RhiBindingType
{
    UniformBuffer,
    UniformBufferDynamic,
    Texture,
    Sampler,
};

struct RhiBufferBinding
{
    RhiBuffer buffer;
    uint32_t offset;
    uint32_t size;
    uint32_t range;
};

struct RhiSamplerBinding
{
    RhiSampler sampler;
};

struct RhiTextureBinding
{
    RhiTexture texture;
};

struct RhiBindGroupEntry
{
    uint32_t binding;
    uint32_t arrayIndex;

    RhiBindingType type;
    union {
        RhiBufferBinding buffer;
        RhiSamplerBinding sampler;
        RhiTextureBinding texture;
    };
};

struct RhiBindGroupDesc
{
    RhiBindGroupLayout layout;
    std::array<RhiBindGroupEntry, 4> entries;
    uint32_t entriesCount;
};

struct RhiBindingDesc
{
    uint32_t binding;
    RhiBindingType type;
    uint32_t arraySize;
    RhiShaderStage stageFlags;
};

struct RhiBindGroupLayoutDesc
{
    uint32_t bindingCount;
    std::array<RhiBindingDesc, 16> bindings;
};

auto RhiCreateBindGroupLayout(const RhiBindGroupLayoutDesc &) -> RhiBindGroupLayout;
void RhiDestroyBindGroupLayout(RhiBindGroupLayout);

auto RhiCreateBindGroup(const RhiBindGroupDesc &) -> RhiBindGroup;
void RhiDestroyBindGroup(RhiBindGroup);

} // namespace nyla