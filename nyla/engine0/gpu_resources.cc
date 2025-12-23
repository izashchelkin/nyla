#include "nyla/engine0/gpu_resources.h"
#include "absl/log/check.h"
#include "nyla/engine0/staging_buffer.h"
#include "nyla/rhi/rhi_bind_groups.h"
#include "third_party/stb/stb_image.h"
#include <cstdint>

namespace nyla
{

namespace
{

RhiBindGroupLayout textureSetLayout;
RhiBindGroupLayout samplerSetLayout;

constexpr uint32_t kStagingBufferSize = 1 << 20;
GpuStagingBuffer *stagingBuffer;

} // namespace

void GpuResourcesInit()
{
    static bool inited = false;
    CHECK(!inited);
    inited = true;

    stagingBuffer = CreateStagingBuffer(kStagingBufferSize);

    textureSetLayout = RhiCreateBindGroupLayout({
        .bindingCount = 1,
        .bindings =
            {
                RhiBindingWriteDesc{
                    .binding = 0,
                    .type = RhiBindingType::Texture,
                    .flags = RhiBindingFlags::VariableCount | RhiBindingFlags::PartiallyBound,
                    .arraySize = 128,
                    .stageFlags = RhiShaderStage::Pixel,
                },
            },
    });

    samplerSetLayout = RhiCreateBindGroupLayout({
        .bindingCount = 1,
        .bindings =
            {
                RhiBindingWriteDesc{
                    .binding = 0,
                    .type = RhiBindingType::Sampler,
                    .flags = RhiBindingFlags::VariableCount | RhiBindingFlags::PartiallyBound,
                    .arraySize = 16,
                    .stageFlags = RhiShaderStage::Pixel,
                },
            },
    });
}

void GpuResourceMakeSampler()
{
    RhiSampler sampler = RhiCreateSampler({});
}

void GpuResourceUploadTexture()
{
    int texWidth = 0;
    int texHeight = 0;
    int texChannels = 0;
    stbi_uc *texPixels = stbi_load("assets/BBreaker/Player.png", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

    RhiTexture texture = RhiCreateTexture({
        .width = static_cast<uint32_t>(texWidth),
        .height = static_cast<uint32_t>(texHeight),
        .memoryUsage = RhiMemoryUsage::GpuOnly,
        .usage = RhiTextureUsage::TransferDst | RhiTextureUsage::ShaderSampled,
        .format = RhiTextureFormat::R8G8B8A8_sRGB,
    });

    free(texPixels);
}

} // namespace nyla