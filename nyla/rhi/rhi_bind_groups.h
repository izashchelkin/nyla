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

struct RhiBindGroupLayout : Handle
{
};

enum class RhiBindingType
{
    UniformBuffer,
    StorageBuffer,
    Texture,
    Sampler,
};

enum class RhiBindingFlags
{
    Dynamic = 1 << 0,
    PartiallyBound = 1 << 1,
    UpdateAfterBind = 1 << 2,
    VariableCount = 1 << 3,
};
NYLA_BITENUM(RhiBindingFlags)

struct RhiBindGroupEntryLayoutDesc
{
    uint32_t binding;
    RhiBindingType type;
    RhiBindingFlags flags;
    uint32_t arraySize;
    RhiShaderStage stageFlags;
};

struct RhiBindGroupLayoutDesc
{
    uint32_t entriesCount;
    std::array<RhiBindGroupEntryLayoutDesc, 4> entries;
};

auto RhiCreateBindGroupLayout(const RhiBindGroupLayoutDesc &) -> RhiBindGroupLayout;
void RhiDestroyBindGroupLayout(RhiBindGroupLayout);

//

struct RhiBindGroup : Handle
{
};

struct RhiBufferBinding
{
    RhiBuffer buffer;
    uint32_t size;
    uint32_t offset;
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

struct RhiBindGroupWriteEntry
{
    uint32_t binding;
    uint32_t arrayIndex;
    union {
        RhiBufferBinding buffer;
        RhiSamplerBinding sampler;
        RhiTextureBinding texture;
    };
};

struct RhiBindGroupWriteDesc
{
    RhiBindGroupLayout layout;
    uint32_t entriesCount;
    std::array<RhiBindGroupWriteEntry, 4> entries;
};

auto RhiCreateBindGroup(RhiBindGroupLayout) -> RhiBindGroup;
void RhiUpdateBindGroup(RhiBindGroup, const RhiBindGroupWriteDesc &);
void RhiDestroyBindGroup(RhiBindGroup);

} // namespace nyla