#include "nyla/commons/containers/inline_vec.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/commons/log.h"
#include "nyla/engine/asset_manager.h"
#include "nyla/engine/engine.h"
#include "nyla/engine/staging_buffer.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_descriptor.h"
#include "nyla/rhi/rhi_pipeline.h"
#include "nyla/rhi/rhi_sampler.h"
#include "nyla/rhi/rhi_shader.h"
#include "nyla/rhi/rhi_texture.h"
#include "third_party/stb/stb_image.h"
#include <cstdint>

namespace nyla
{

void AssetManager::Init()
{
    static bool inited = false;
    NYLA_ASSERT(!inited);
    inited = true;

    //

    const std::array<RhiDescriptorLayoutDesc, 2> descriptors{
        RhiDescriptorLayoutDesc{
            .binding = 0,
            .type = RhiBindingType::Sampler,
            .flags = RhiDescriptorFlags::PartiallyBound,
            .arraySize = m_Samplers.max_size(),
            .stageFlags = RhiShaderStage::Pixel,
        },
        RhiDescriptorLayoutDesc{
            .binding = 1,
            .type = RhiBindingType::Texture,
            .flags = RhiDescriptorFlags::PartiallyBound,
            .arraySize = m_Textures.max_size(),
            .stageFlags = RhiShaderStage::Pixel,
        },
    };
    m_DescriptorSetLayout = g_Rhi->CreateDescriptorSetLayout(RhiDescriptorSetLayoutDesc{
        .descriptors = descriptors,
    });

    m_DescriptorSet = g_Rhi->CreateDescriptorSet(m_DescriptorSetLayout);

    //

    m_Samplers.resize(4);
    InlineVec<RhiDescriptorWriteDesc, 4> descriptorWrites;

    auto addSampler = [this, &descriptorWrites](SamplerType samplerType, RhiFilter filter,
                                                RhiSamplerAddressMode addressMode) -> void {
        RhiSampler sampler = g_Rhi->CreateSampler({
            .minFilter = filter,
            .magFilter = filter,
            .addressModeU = addressMode,
            .addressModeV = addressMode,
            .addressModeW = addressMode,
        });

        auto index = static_cast<uint32_t>(samplerType);
        m_Samplers[index] = SamplerData{.sampler = sampler};

        descriptorWrites.emplace_back(RhiDescriptorWriteDesc{
            .set = m_DescriptorSet,
            .binding = kSamplersDescriptorBinding,
            .arrayIndex = index,
            .type = RhiBindingType::Sampler,
            .resourceBinding = {.sampler = {.sampler = sampler}},
        });
    };
    addSampler(SamplerType::LinearClamp, RhiFilter::Linear, RhiSamplerAddressMode::ClampToEdge);
    addSampler(SamplerType::LinearRepeat, RhiFilter::Linear, RhiSamplerAddressMode::Repeat);
    addSampler(SamplerType::NearestClamp, RhiFilter::Nearest, RhiSamplerAddressMode::ClampToEdge);
    addSampler(SamplerType::NearestRepeat, RhiFilter::Nearest, RhiSamplerAddressMode::Repeat);

    g_Rhi->WriteDescriptors(descriptorWrites);
}

auto AssetManager::GetDescriptorSetLayout() -> RhiDescriptorSetLayout
{
    return m_DescriptorSetLayout;
}

void AssetManager::BindDescriptorSet(RhiCmdList cmd)
{
    g_Rhi->CmdBindGraphicsBindGroup(cmd, 1, m_DescriptorSet, {});
}

void AssetManager::Upload(RhiCmdList cmd)
{
    InlineVec<RhiDescriptorWriteDesc, 256> descriptorWrites;

    for (uint32_t i = 0; i < m_Textures.size(); ++i)
    {
        auto &slot = *(m_Textures.begin() + i);
        if (!slot.used)
            continue;

        TextureData &textureAssetData = slot.data;
        if (!textureAssetData.needsUpload)
            continue;

        void *data = stbi_load(textureAssetData.path.c_str(), (int *)&textureAssetData.width,
                               (int *)&textureAssetData.height, (int *)&textureAssetData.channels, STBI_rgb_alpha);
        if (!data)
        {
            NYLA_LOG("stbi_load failed for '%s': %s", textureAssetData.path.data(), stbi_failure_reason());
            NYLA_ASSERT(false);
        }

        const RhiTexture texture = g_Rhi->CreateTexture(RhiTextureDesc{
            .width = textureAssetData.width,
            .height = textureAssetData.height,
            .memoryUsage = RhiMemoryUsage::GpuOnly,
            .usage = RhiTextureUsage::TransferDst | RhiTextureUsage::ShaderSampled,
            .format = RhiTextureFormat::R8G8B8A8_sRGB,
        });
        textureAssetData.texture = texture;

        const RhiTextureView textureView = g_Rhi->CreateTextureView(RhiTextureViewDesc{
            .texture = texture,
        });
        textureAssetData.textureView = textureView;

        descriptorWrites.emplace_back(RhiDescriptorWriteDesc{
            .set = m_DescriptorSet,
            .binding = kTexturesDescriptorBinding,
            .arrayIndex = i,
            .type = RhiBindingType::Texture,
            .resourceBinding =
                RhiDescriptorResourceBinding{
                    .texture =
                        RhiTextureBinding{
                            .textureView = textureView,
                        },
                },
        });

        g_Rhi->CmdTransitionTexture(cmd, texture, RhiTextureState::TransferDst);

        const uint32_t size = textureAssetData.width * textureAssetData.height * textureAssetData.channels;
        char *uploadMemory = StagingBufferCopyIntoTexture(cmd, g_StagingBuffer, texture, size);
        memcpy(uploadMemory, data, size);

        free(data);

        // TODO: this barrier does not need to be here
        g_Rhi->CmdTransitionTexture(cmd, texture, RhiTextureState::ShaderRead);

        NYLA_LOG("Uploading '%s'", (const char *)textureAssetData.path.data());

        textureAssetData.needsUpload = false;
    }

    if (!descriptorWrites.empty())
        g_Rhi->WriteDescriptors(descriptorWrites);
}

auto AssetManager::DeclareTexture(std::string_view path) -> Texture
{
    return m_Textures.Acquire(TextureData{
        .path = std::string{path},
        .needsUpload = true,
    });
}

AssetManager *g_AssetManager = new AssetManager{};

} // namespace nyla