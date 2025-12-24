#include "nyla/engine0/gpu_resources.h"
#include "absl/log/check.h"
#include "nyla/commons/containers/inline_vec.h"
#include "nyla/engine0/staging_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_descriptor.h"
#include "nyla/rhi/rhi_pipeline.h"
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
    using ResourceType = nyla::GpuResource<HandleType>;

    std::array<GpuResourceSlot, Size> slots;

    [[nodiscard]]
    constexpr auto size() const -> uint32_t
    {
        return Size;
    }

    [[nodiscard]]
    constexpr auto GetStages() const -> RhiShaderStage
    {
        return Stages;
    }

    [[nodiscard]]
    constexpr auto GetBindingType() const -> RhiBindingType
    {
        return BindingType;
    }

    [[nodiscard]]
    constexpr auto GetBinding() const -> uint32_t
    {
        return Binding;
    }
};

RhiDescriptorSetLayout descriptorSetLayout;
RhiDescriptorSet descriptorSet;

GpuResourcePool<1 << 5, RhiSampler, RhiBindingType::Sampler, RhiShaderStage::Pixel, 0> samplerPool;
GpuResourcePool<1 << 8, RhiTexture, RhiBindingType::Texture, RhiShaderStage::Pixel, 1> texturePool;

template <uint32_t Size, typename HandleType, RhiBindingType BindingType, RhiShaderStage Stages, uint32_t Binding>
auto AcquireGpuResourceSlot(GpuResourcePool<Size, HandleType, BindingType, Stages, Binding> &pool,
                            RhiDescriptorResourceBinding resourceBinding, HandleType handle)
    -> std::remove_reference_t<decltype(pool)>::ResourceType
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

auto GpuResourceGetDescriptorSetLayout() -> RhiDescriptorSetLayout
{
    return descriptorSetLayout;
}

void GpuResourcesInit()
{
    static bool inited = false;
    CHECK(!inited);
    inited = true;

    auto makeDescriptorLayout = [](const auto &pool) -> RhiDescriptorLayoutDesc {
        return RhiDescriptorLayoutDesc{
            .binding = pool.GetBinding(),
            .type = pool.GetBindingType(),
            .flags = RhiDescriptorFlags::PartiallyBound,
            .arraySize = pool.size(),
            .stageFlags = pool.GetStages(),
        };
    };

    const std::array<RhiDescriptorLayoutDesc, 2> descriptorLayouts{
        makeDescriptorLayout(samplerPool),
        makeDescriptorLayout(texturePool),
    };

    descriptorSetLayout = RhiCreateDescriptorSetLayout(RhiDescriptorSetLayoutDesc{
        .descriptors = descriptorLayouts,
    });
    descriptorSet = RhiCreateDescriptorSet(descriptorSetLayout);
}

#if 0
    int texWidth = 0;
    int texHeight = 0;
    int texChannels = 0;
    stbi_uc *texPixels = stbi_load("assets/BBreaker/Player.png", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

    free(texPixels);
#endif

void GpuResourcesBind(RhiCmdList cmd)
{
    RhiCmdBindGraphicsBindGroup(cmd, 1, descriptorSet, {});
}

void GpuResourcesWriteDescriptors()
{
    InlineVec<RhiDescriptorWriteDesc, (texturePool.size() + samplerPool.size()) / 2> writes;

    auto processPool = [&writes](auto &pool) -> void {
        for (uint32_t i = 0; i < pool.size(); ++i)
        {
            auto &slot = pool.slots[i];
            if (!(slot.flags & GpuResourceSlot::kFlagUsed))
                continue;
            if (!(slot.flags & GpuResourceSlot::kFlagDirty))
                continue;

            writes.emplace_back(RhiDescriptorWriteDesc{
                .set = descriptorSet,
                .binding = pool.GetBinding(),
                .arrayIndex = i,
                .type = pool.GetBindingType(),
                .resourceBinding = slot.resourceBinding,
            });
            slot.flags &= ~GpuResourceSlot::kFlagDirty;
        }
    };
    processPool(texturePool);
    processPool(samplerPool);

    if (!writes.empty())
        RhiWriteDescriptors(writes);
}

auto CreateTexture(uint32_t width, uint32_t height, RhiTextureFormat format) -> Texture
{
    const RhiTexture texture = RhiCreateTexture({
        .width = width,
        .height = height,
        .memoryUsage = RhiMemoryUsage::GpuOnly,
        .usage = RhiTextureUsage::TransferDst | RhiTextureUsage::ShaderSampled,
        .format = format,
    });

    const RhiDescriptorResourceBinding resourceBinding{
        .texture = {.texture = texture},
    };

    return AcquireGpuResourceSlot(texturePool, resourceBinding, texture);
}

void UploadTexture(RhiCmdList cmd, GpuStagingBuffer *stagingBuffer, Texture &texture, std::span<const std::byte> pixels)
{
    RhiCmdTransitionTexture(cmd, texture.handle, RhiTextureState::TransferDst);

    char *dst = StagingBufferCopyIntoTexture(cmd, stagingBuffer, texture.handle, pixels.size());
    memcpy(dst, pixels.data(), pixels.size());

    RhiCmdTransitionTexture(cmd, texture.handle, RhiTextureState::ShaderRead);
}

auto CreateSampler() -> Sampler
{
    const RhiSampler sampler = RhiCreateSampler({});

    const RhiDescriptorResourceBinding resourceBinding{
        .sampler = {.sampler = sampler},
    };

    return AcquireGpuResourceSlot(samplerPool, resourceBinding, sampler);
}

} // namespace nyla