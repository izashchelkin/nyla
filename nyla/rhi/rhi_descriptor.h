#pragma once

#include "nyla/commons/handle.h"
#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_sampler.h"
#include "nyla/rhi/rhi_shader.h"
#include "nyla/rhi/rhi_texture.h"
#include <cstdint>

namespace nyla
{

struct RhiDescriptorSetLayout : Handle
{
};

enum class RhiBindingType
{
    UniformBuffer,
    StorageBuffer,
    Texture,
    Sampler,
};

enum class RhiDescriptorFlags
{
    Dynamic = 1 << 0,
    PartiallyBound = 1 << 1,
    UpdateAfterBind = 1 << 2,
    VariableCount = 1 << 3,
};
NYLA_BITENUM(RhiDescriptorFlags)

struct RhiDescriptorLayoutDesc
{
    uint32_t binding;
    RhiBindingType type;
    RhiDescriptorFlags flags;
    uint32_t arraySize;
    RhiShaderStage stageFlags;
};

struct RhiDescriptorSetLayoutDesc
{
    std::span<const RhiDescriptorLayoutDesc> descriptors;
};

//

struct RhiDescriptorSet : Handle
{
};

//

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
    RhiTextureView textureView;
};

union RhiDescriptorResourceBinding {
    RhiBufferBinding buffer;
    RhiTextureBinding texture;
    RhiSamplerBinding sampler;
};

struct RhiDescriptorWriteDesc
{
    RhiDescriptorSet set;
    uint32_t binding;
    uint32_t arrayIndex;
    RhiBindingType type;
    RhiDescriptorResourceBinding resourceBinding;
};

} // namespace nyla