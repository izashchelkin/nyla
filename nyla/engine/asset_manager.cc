#include "nyla/engine/asset_manager.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/commons/log.h"
#include "nyla/commons/memory/region_alloc.h"
#include "nyla/engine/engine.h"
#include "nyla/engine/staging_buffer.h"
#include "nyla/formats/gltf/gltf_parser.h"
#include "nyla/platform/platform.h"
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

    for (uint32_t i = 0; i < m_Meshes.size(); ++i)
    {
        auto &slot = *(m_Meshes.begin() + i);
        if (!slot.used)
            continue;

        MeshData &meshData = slot.data;
        if (!meshData.needsUpload)
            continue;

        std::vector<std::byte> data = g_Platform->ReadFile(meshData.path);

        RegionAlloc &transientAlloc = g_Engine->GetCpuAllocs()->transient;
        RegionAlloc scratchAlloc = transientAlloc.PushSubAlloc(16_KiB);
        {
            GltfParser parser;
            parser.Init(&scratchAlloc, data.data(), data.size());
            NYLA_ASSERT(parser.Parse());

            for (auto mesh : parser.GetMeshes())
            {
                NYLA_LOG("Mesh: " NYLA_SV_FMT, NYLA_SV_ARG(mesh.name));

                for (auto primitive : mesh.primitives)
                {
                    NYLA_LOG(" AttributesCount: %lu", primitive.attributes.size());
                    for (auto attribute : primitive.attributes)
                    {
                        auto &accessor = parser.GetAccessor(attribute.accessor);
                        auto &bufferView = parser.GetBufferView(accessor.bufferView);

                        std::span<char> data = parser.GetBufferViewData(bufferView);

                        NYLA_LOG("  Attribute: " NYLA_SV_FMT ": %d    BufferViewLength: %d",
                                 NYLA_SV_ARG(attribute.name), attribute.accessor, bufferView.byteLength);
                    }
                }
            }
        }
        transientAlloc.Pop(scratchAlloc.GetBase());

        meshData.needsUpload = false;
    }

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

        const RhiSampledTextureView textureView = g_Rhi->CreateSampledTextureView(RhiTextureViewDesc{
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

auto AssetManager::DeclareMesh(std::string_view path) -> Mesh
{
    return m_Meshes.Acquire(MeshData{
        .path = std::string{path},
        .needsUpload = true,
    });
}

} // namespace nyla