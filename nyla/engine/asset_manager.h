#pragma once

#include "nyla/commons/handle.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/engine/engine.h"
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
    AssetManager() : m_Textures{}
    {
    }

    struct Texture : Handle
    {
    };

    struct Mesh : Handle
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

    auto DeclareTexture(std::string_view path) -> Texture;
    auto DeclareMesh(std::string_view path) -> Mesh;

  private:
    struct TextureData
    {
        std::string path;
        RhiTexture texture;
        RhiSampledTextureView textureView;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t channels = 0;
        bool needsUpload;
    };
    HandlePool<Texture, TextureData, 128> m_Textures;

    struct MeshData
    {
    };
    HandlePool<Mesh, MeshData, 128> m_Meshes;
};

extern AssetManager *g_AssetManager;

} // namespace nyla