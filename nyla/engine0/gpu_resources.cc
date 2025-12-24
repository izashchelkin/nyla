#include "nyla/engine0/gpu_resources.h"
#include "absl/log/check.h"
#include "nyla/engine0/staging_buffer.h"
#include "nyla/rhi/rhi_descriptor.h"
#include "nyla/rhi/rhi_sampler.h"
#include "nyla/rhi/rhi_shader.h"
#include "nyla/rhi/rhi_texture.h"
#include "third_party/stb/stb_image.h"
#include <cstdint>
#include <type_traits>

namespace nyla
{

namespace
{

RhiDescriptorSetLayout textureSetLayout;
RhiDescriptorSetLayout samplerSetLayout;

constexpr uint32_t kStagingBufferSize = 1 << 20;
GpuStagingBuffer *stagingBuffer;

struct GpuResourceSlot
{
    static constexpr uint32_t kFlagUsed = 1 << 0;
    static constexpr uint32_t kFlagDirty = 1 << 1;

    uint32_t flags;
    uint32_t gen;
    RhiDescriptorResourceBinding resourceBinding;
};

template <uint32_t Size, typename HandleType, RhiBindingType BindingType, RhiShaderStage Stages, uint32_t Binding>
struct GpuResourcePool
{
    std::array<GpuResourceSlot, Size> slots;

    RhiDescriptorSetLayout descriptorSetLayout;
    RhiDescriptorSet descriptorSet;

    constexpr auto size() -> uint32_t
    {
        return Size;
    }

    constexpr auto GetStages() -> RhiShaderStage
    {
        return Stages;
    }

    constexpr auto GetBindingType() -> RhiBindingType
    {
        return BindingType;
    }

    constexpr auto GetBinding() -> uint32_t
    {
        return Binding;
    }

    struct GpuResource
    {
        uint32_t gen;
        uint32_t index;
        HandleType handle;
    };
};

GpuResourcePool<1 << 8, RhiTexture, RhiBindingType::Texture, RhiShaderStage::Pixel, 0> texturesPool;
GpuResourcePool<1 << 5, RhiSampler, RhiBindingType::Sampler, RhiShaderStage::Pixel, 0> samplersPool;

template <uint32_t Size, typename HandleType, RhiBindingType BindingType, RhiShaderStage Stages, uint32_t Binding>
void InitGpuResourcePool(GpuResourcePool<Size, HandleType, BindingType, Stages, Binding> &pool)
{
    const RhiDescriptorLayoutDesc descriptorLayout{
        .binding = pool.GetBinding(),
        .type = BindingType,
        .flags = RhiDescriptorFlags::VariableCount | RhiDescriptorFlags::PartiallyBound |
                 RhiDescriptorFlags::UpdateAfterBind,
        .arraySize = pool.size(),
        .stageFlags = pool.GetStages(),
    };

    pool.descriptorSetLayout = RhiCreateDescriptorSetLayout(RhiDescriptorSetLayoutDesc{
        .descriptors = std::span{&descriptorLayout, 1},
    });
    pool.descriptorSet = RhiCreateDescriptorSet(pool.descriptorSetLayout);
}

template <uint32_t Size, typename HandleType, RhiBindingType BindingType, RhiShaderStage Stages, uint32_t Binding>
auto AcquireGpuResourceSlot(GpuResourcePool<Size, HandleType, BindingType, Stages, Binding> &pool,
                            RhiDescriptorResourceBinding resourceBinding, HandleType handle)
    -> std::remove_reference_t<decltype(pool)>::GpuResource
{
    for (uint32_t i = 0; i < pool.size(); ++i)
    {
        GpuResourceSlot &slot = pool.slots[i];
        if (slot.flags & GpuResourceSlot::kFlagUsed)
            continue;

        slot.resourceBinding = resourceBinding;
        slot.flags = GpuResourceSlot::kFlagUsed | GpuResourceSlot::kFlagDirty;
        ++slot.gen;

        return {
            .gen = slot.gen,
            .index = i,
            .handle = handle,
        };
    }
    CHECK(false);
    return {};
}

} // namespace

void GpuResourcesInit()
{
    static bool inited = false;
    CHECK(!inited);
    inited = true;

    stagingBuffer = CreateStagingBuffer(kStagingBufferSize);

    InitGpuResourcePool(texturesPool);
    InitGpuResourcePool(samplersPool);
}

#if 0
    int texWidth = 0;
    int texHeight = 0;
    int texChannels = 0;
    stbi_uc *texPixels = stbi_load("assets/BBreaker/Player.png", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

    free(texPixels);
#endif

void GpuResourcesWriteDescriptors()
{
    std::array<RhiDescriptorWriteDesc, (texturesPool.size() + samplersPool.size()) / 2> writes;
    uint32_t writesCount;

    auto processPool = [&writes, &writesCount](auto pool) -> void {
        for (uint32_t i = 0; i < texturesPool.size(); ++i)
        {
            auto &slot = pool.slots[i];
            if (!(slot.flags & GpuResourceSlot::kFlagUsed))
                continue;
            if (!(slot.flags & GpuResourceSlot::kFlagDirty))
                continue;

            writes[writesCount++] = RhiDescriptorWriteDesc{
                .set = pool.descriptorSet,
                .binding = pool.GetBinding(),
                .arrayIndex = i,
                .type = pool.GetBindingType(),
                .resourceBinding = slot.resourceBinding,
            };
            slot.flags &= ~GpuResourceSlot::kFlagDirty;
        }
    };
    processPool(texturesPool);
    processPool(samplersPool);
}

auto CreateSampledTextureResource(uint32_t width, uint32_t height, RhiTextureFormat format)
    -> decltype(texturesPool)::GpuResource
{
    RhiTexture texture = RhiCreateTexture({
        .width = width,
        .height = height,
        .memoryUsage = RhiMemoryUsage::GpuOnly,
        .usage = RhiTextureUsage::TransferDst | RhiTextureUsage::ShaderSampled,
        .format = format,
    });

    RhiDescriptorResourceBinding resourceBinding{
        .texture = {.texture = texture},
    };

    return AcquireGpuResourceSlot(texturesPool, resourceBinding, texture);
}

auto CreateSamplerResource() -> decltype(samplersPool)::GpuResource
{
    RhiSampler sampler = RhiCreateSampler({});

    RhiDescriptorResourceBinding resourceBinding{
        .sampler = {.sampler = sampler},
    };

    return AcquireGpuResourceSlot(samplersPool, resourceBinding, sampler);
}

} // namespace nyla