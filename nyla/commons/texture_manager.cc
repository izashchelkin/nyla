#include "nyla/commons/texture_manager.h"

#include <cinttypes>
#include <cstdint>

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/asset_file_format.h"
#include "nyla/commons/asset_manager.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/gpu_upload.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

namespace
{

enum class texture_state
{
    NotUploaded = 0,
    Uploaded
};

struct texture_metadata
{
    uint64_t guid;
    texture_state state;
    rhi_texture_format textureFormat;
    rhi_texture texture;
    rhi_srv textureView;
    rhi_texture pendingDestroyTexture;
    rhi_srv pendingDestroyView;
    uint32_t width;
    uint32_t height;
    uint32_t channels;
};

struct texture_manager
{
    handle_pool<texture_handle, texture_metadata, 128> textures;
};

texture_manager *manager;

void OnAssetChanged(uint64_t guid, byteview, void *)
{
    for (auto &slot : manager->textures)
    {
        if (!slot.used)
            continue;

        texture_metadata &metadata = slot.data;
        if (metadata.guid != guid)
            continue;

        if (metadata.state == texture_state::Uploaded)
        {
            metadata.pendingDestroyTexture = metadata.texture;
            metadata.pendingDestroyView = metadata.textureView;
            metadata.texture = {};
            metadata.textureView = {};
        }
        metadata.state = texture_state::NotUploaded;
    }
}

} // namespace

namespace TextureManager
{

void API Bootstrap()
{
    manager = &RegionAlloc::Alloc<texture_manager>(RegionAlloc::g_BootstrapAlloc);
    AssetManager::Subscribe(OnAssetChanged, nullptr);
}

void API Update(rhi_cmdlist cmd)
{
    for (auto &slot : manager->textures)
    {
        if (!slot.used)
            continue;

        texture_metadata &metadata = slot.data;
        if (metadata.state != texture_state::NotUploaded)
            continue;

        if (metadata.pendingDestroyTexture || metadata.pendingDestroyView)
        {
            Rhi::WaitGpuIdle();
            if (metadata.pendingDestroyView)
                Rhi::DestroySampledTextureView(metadata.pendingDestroyView);
            if (metadata.pendingDestroyTexture)
                Rhi::DestroyTexture(metadata.pendingDestroyTexture);
            metadata.pendingDestroyTexture = {};
            metadata.pendingDestroyView = {};
        }

        LOG("Uploading texture '%" PRIu64 "'", metadata.guid);

        byteview rawBytes = AssetManager::Get(metadata.guid);
        ASSERT(rawBytes.size >= sizeof(texture_blob_header));
        auto *header = (const texture_blob_header *)rawBytes.data;

        metadata.width = header->width;
        metadata.height = header->height;
        metadata.channels = 4;

        const uint8_t *pixelData = rawBytes.data + header->pixelOffset;

        const rhi_texture texture = Rhi::CreateTexture(rhi_texture_desc{
            .width = metadata.width,
            .height = metadata.height,
            .memoryUsage = rhi_memory_usage::GpuOnly,
            .usage = rhi_texture_usage::TransferDst | rhi_texture_usage::ShaderSampled,
            .format = rhi_texture_format::R8G8B8A8_sRGB,
        });
        metadata.texture = texture;

        const rhi_srv textureView = Rhi::CreateSampledTextureView(rhi_texture_view_desc{
            .texture = texture,
        });
        metadata.textureView = textureView;

        Rhi::CmdTransitionTexture(cmd, texture, rhi_texture_state::TransferDst);

        const uint32_t byteSize = metadata.width * metadata.height * metadata.channels;
        ASSERT(rawBytes.size >= header->pixelOffset + byteSize);

        char *uploadMemory = GpuUpload::CmdCopyTexture(cmd, texture, byteSize);
        MemCpy(uploadMemory, pixelData, byteSize);

        // TODO: this is suboptimal - move to where it's used
        Rhi::CmdTransitionTexture(cmd, texture, rhi_texture_state::ShaderRead);
        metadata.state = texture_state::Uploaded;
    }
}

auto API DeclareTexture(uint64_t guid) -> texture_handle
{
    return HandlePool::Acquire(manager->textures, texture_metadata{
                                                      .guid = guid,
                                                      .state = texture_state::NotUploaded,
                                                  });
}

auto API GetSRV(texture_handle Texture) -> rhi_srv
{
    if (Texture)
        return HandlePool::ResolveData(manager->textures, Texture).textureView;
    else
        return {};
}

} // namespace TextureManager

} // namespace nyla