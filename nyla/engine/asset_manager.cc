#include "nyla/engine/asset_manager.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/commons/log.h"
#include "nyla/engine/staging_buffer.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_sampler.h"
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

    auto addSampler = [this](SamplerType samplerType, RhiFilter filter, RhiSamplerAddressMode addressMode) -> void {
        RhiSampler sampler = g_Rhi->CreateSampler({
            .minFilter = filter,
            .magFilter = filter,
            .addressModeU = addressMode,
            .addressModeV = addressMode,
            .addressModeW = addressMode,
        });
    };
    addSampler(SamplerType::LinearClamp, RhiFilter::Linear, RhiSamplerAddressMode::ClampToEdge);
    addSampler(SamplerType::LinearRepeat, RhiFilter::Linear, RhiSamplerAddressMode::Repeat);
    addSampler(SamplerType::NearestClamp, RhiFilter::Nearest, RhiSamplerAddressMode::ClampToEdge);
    addSampler(SamplerType::NearestRepeat, RhiFilter::Nearest, RhiSamplerAddressMode::Repeat);
}

void AssetManager::Upload(RhiCmdList cmd)
{
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