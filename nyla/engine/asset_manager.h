#pragma once

#include "nyla/commons/containers/inline_vec.h"
#include "nyla/commons/handle.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/rhi/rhi_descriptor.h"
#include "nyla/rhi/rhi_sampler.h"
#include "nyla/rhi/rhi_texture.h"
#include <cstdint>
#include <string>

namespace nyla
{

class AssetManager
{
    constexpr static uint32_t kSamplersDescriptorBinding = 0;
    constexpr static uint32_t kTexturesDescriptorBinding = 1;

  public:
    AssetManager() : m_Samplers{}, m_Textures{}
    {
    }

    struct Texture : Handle
    {
    };

    enum class SamplerType
    {
        LinearClamp = 0,
        LinearRepeat = 1,
        NearestClamp = 2,
        NearestRepeat = 3,
    };

    void Init();
    void Upload(RhiCmdList cmd);

    auto GetDescriptorSetLayout() -> RhiDescriptorSetLayout;
    void BindDescriptorSet(RhiCmdList);

    auto DeclareTexture(std::string_view path) -> Texture;

  private:
    struct TextureData
    {
        std::string path;
        RhiTexture texture;
        RhiTextureView textureView;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t channels = 0;
        bool needsUpload;
    };
    HandlePool<Texture, TextureData, 128> m_Textures;

    struct SamplerData
    {
        RhiSampler sampler;
    };
    InlineVec<SamplerData, 4> m_Samplers;

    RhiDescriptorSetLayout m_DescriptorSetLayout;
    RhiDescriptorSet m_DescriptorSet;
};

extern AssetManager *g_AssetManager;

} // namespace nyla