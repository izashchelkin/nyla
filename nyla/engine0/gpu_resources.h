#pragma once

#include "nyla/commons/containers/inline_vec.h"
#include "nyla/commons/handle.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/engine0/staging_buffer.h"
#include "nyla/rhi/rhi_descriptor.h"
#include "nyla/rhi/rhi_sampler.h"
#include "nyla/rhi/rhi_texture.h"
#include <cstdint>

namespace nyla
{

class AssetManager
{
    constexpr static uint32_t kSamplersDescriptorBinding = 0;
    constexpr static uint32_t kTexturesDescriptorBinding = 1;

  public:
    AssetManager(GpuStagingBuffer *stagingBuffer) : m_stagingBuffer{stagingBuffer}, m_samplers{}, m_textures{}
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
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t channels = 0;
        bool needsUpload;
    };
    HandlePool<Texture, TextureData, 128> m_textures;

    struct SamplerData
    {
        RhiSampler sampler;
    };
    InlineVec<SamplerData, 4> m_samplers;

    GpuStagingBuffer *m_stagingBuffer;

    RhiDescriptorSetLayout m_descriptorSetLayout;
    RhiDescriptorSet m_descriptorSet;
};

} // namespace nyla