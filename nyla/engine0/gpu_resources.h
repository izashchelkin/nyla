#pragma once

#include "nyla/engine0/staging_buffer.h"
#include "nyla/rhi/rhi_descriptor.h"
#include "nyla/rhi/rhi_sampler.h"
#include "nyla/rhi/rhi_texture.h"
#include <cstdint>
#include <span>

namespace nyla
{

template <typename HandleType> struct GpuResource
{
    uint32_t gen;
    uint32_t index;
    HandleType handle;
};

using Texture = GpuResource<RhiTexture>;
using Sampler = GpuResource<RhiSampler>;

void GpuResourcesInit();
auto GpuResourceGetDescriptorSetLayout() -> RhiDescriptorSetLayout;
void GpuResourcesWriteDescriptors();
void GpuResourcesBind(RhiCmdList cmd);

auto CreateTexture(uint32_t width, uint32_t height, RhiTextureFormat format) -> Texture;
auto CreateSampler() -> Sampler;

void UploadTexture(RhiCmdList cmd, GpuStagingBuffer *stagingBuffer, Texture &texture,
                   std::span<const std::byte> pixels);

} // namespace nyla