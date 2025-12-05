#pragma once

#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_handle.h"
#include "nyla/rhi/rhi_shader.h"

namespace nyla
{

constexpr inline uint32_t kRhiMaxBindGroupLayouts = 4;

struct RhiBindGroup : RhiHandle
{
};
struct RhiBindGroupLayout : RhiHandle
{
};

enum class RhiBindingType
{
    UniformBuffer,
    UniformBufferDynamic
};

struct RhiBufferBinding
{
    RhiBuffer buffer;
    uint32_t offset;
    uint32_t size;
};

struct RhiBindGroupEntry
{
    uint32_t binding;
    RhiBindingType type;
    uint32_t arrayIndex;
    union {
        RhiBufferBinding buffer;
    };
};

struct RhiBindGroupDesc
{
    RhiBindGroupLayout layout;
    RhiBindGroupEntry entries[4];
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
    RhiBindingDesc bindings[16];
    uint32_t bindingCount;
};

auto RhiCreateBindGroupLayout(const RhiBindGroupLayoutDesc &) -> RhiBindGroupLayout;
void RhiDestroyBindGroupLayout(RhiBindGroupLayout);

auto RhiCreateBindGroup(const RhiBindGroupDesc &) -> RhiBindGroup;
void RhiDestroyBindGroup(RhiBindGroup);

} // namespace nyla