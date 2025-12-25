#include "nyla/engine0/asset_manager.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "nyla/commons/containers/inline_vec.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/engine0/staging_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_descriptor.h"
#include "nyla/rhi/rhi_pipeline.h"
#include "nyla/rhi/rhi_sampler.h"
#include "nyla/rhi/rhi_shader.h"
#include "nyla/rhi/rhi_texture.h"
#include "third_party/stb/stb_image.h"
#include <cstdint>
#include <libintl.h>

namespace nyla
{

void AssetManager::Init()
{
    static bool inited = false;
    CHECK(!inited);
    inited = true;

    //

    const std::array<RhiDescriptorLayoutDesc, 2> descriptors{
        RhiDescriptorLayoutDesc{
            .binding = 0,
            .type = RhiBindingType::Sampler,
            .flags = RhiDescriptorFlags::PartiallyBound,
            .arraySize = m_samplers.max_size(),
            .stageFlags = RhiShaderStage::Pixel,
        },
        RhiDescriptorLayoutDesc{
            .binding = 1,
            .type = RhiBindingType::Texture,
            .flags = RhiDescriptorFlags::PartiallyBound,
            .arraySize = m_textures.max_size(),
            .stageFlags = RhiShaderStage::Pixel,
        },
    };
    m_descriptorSetLayout = RhiCreateDescriptorSetLayout(RhiDescriptorSetLayoutDesc{
        .descriptors = descriptors,
    });

    m_descriptorSet = RhiCreateDescriptorSet(m_descriptorSetLayout);

    //

    m_samplers.resize(4);
    InlineVec<RhiDescriptorWriteDesc, 4> descriptorWrites;

    auto addSampler = [this, &descriptorWrites](SamplerType samplerType, RhiFilter filter,
                                                RhiSamplerAddressMode addressMode) -> void {
        RhiSampler sampler = RhiCreateSampler({
            .minFilter = filter,
            .magFilter = filter,
            .addressModeU = addressMode,
            .addressModeV = addressMode,
            .addressModeW = addressMode,
        });

        auto index = static_cast<uint32_t>(samplerType);
        m_samplers[index] = SamplerData{.sampler = sampler};

        descriptorWrites.emplace_back(RhiDescriptorWriteDesc{
            .set = m_descriptorSet,
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

    RhiWriteDescriptors(descriptorWrites);
}

auto AssetManager::GetDescriptorSetLayout() -> RhiDescriptorSetLayout
{
    return m_descriptorSetLayout;
}

void AssetManager::BindDescriptorSet(RhiCmdList cmd)
{
    RhiCmdBindGraphicsBindGroup(cmd, 1, m_descriptorSet, {});
}

void AssetManager::Upload(RhiCmdList cmd)
{
    InlineVec<RhiDescriptorWriteDesc, 256> descriptorWrites;

    for (uint32_t i = 0; i < m_textures.size(); ++i)
    {
        auto &slot = *(m_textures.begin() + i);
        if (!slot.used)
            continue;

        TextureData &textureData = slot.data;
        if (!textureData.needsUpload)
            continue;

        void *data = stbi_load(textureData.path.c_str(), (int *)&textureData.width, (int *)&textureData.height,
                               (int *)&textureData.channels, STBI_rgb_alpha);
        CHECK(data) << "stbi_load failed for '" << textureData.path << "': " << stbi_failure_reason();

        textureData.texture = RhiCreateTexture({
            .width = textureData.width,
            .height = textureData.height,
            .memoryUsage = RhiMemoryUsage::GpuOnly,
            .usage = RhiTextureUsage::TransferDst | RhiTextureUsage::ShaderSampled,
            .format = RhiTextureFormat::R8G8B8A8_sRGB,
        });
        const RhiTexture texture = textureData.texture;

        descriptorWrites.emplace_back(RhiDescriptorWriteDesc{
            .set = m_descriptorSet,
            .binding = kTexturesDescriptorBinding,
            .arrayIndex = i,
            .type = RhiBindingType::Texture,
            .resourceBinding = {.texture = {.texture = texture}},
        });

        RhiCmdTransitionTexture(cmd, texture, RhiTextureState::TransferDst);

        const uint32_t size = textureData.width * textureData.height * textureData.channels;
        char *uploadMemory = StagingBufferCopyIntoTexture(cmd, m_stagingBuffer, texture, size);
        memcpy(uploadMemory, data, size);

        free(data);

        // TODO: this barrier does not need to be here
        RhiCmdTransitionTexture(cmd, texture, RhiTextureState::ShaderRead);

        LOG(INFO) << "Uploading '" << textureData.path << "'";

        textureData.needsUpload = false;
    }

    if (!descriptorWrites.empty())
        RhiWriteDescriptors(descriptorWrites);
}

auto AssetManager::DeclareTexture(std::string_view path) -> Texture
{
    return m_textures.Acquire(TextureData{
        .path = std::string{path},
        .needsUpload = true,
    });
}

} // namespace nyla