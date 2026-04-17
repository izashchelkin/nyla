#include "nyla/commons/texture_manager.h"

#include <cinttypes>
#include <cstdint>

#include "nyla/commons/asset_file.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/gpu_upload.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/span_def.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "nyla/commons/stb_image.h"

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
    rhi_texture texture;
    rhi_stv textureView;
    uint32_t width;
    uint32_t height;
    uint32_t channels;
};

struct texture_manager
{
    handle_pool<texture, texture_metadata, 128> textures;
};

texture_manager *manager;

} // namespace

namespace TextureManager
{

void API Bootstrap()
{
    manager = &RegionAlloc::Alloc<texture_manager>(RegionAlloc::g_BootstrapAlloc);
}

void API Update(rhi_cmdlist cmd, byteview assetFile)
{
    for (auto &slot : manager->textures)
    {
        if (!slot.used)
            continue;

        texture_metadata &metadata = slot.data;
        if (metadata.state != texture_state::NotUploaded)
            continue;

        LOG("Uploading texture '" SV_FMT "'", metadata.guid);

        byteview rawBytes = AssetFileGetData(assetFile, metadata.guid);

        uint8_t *pixelData = stbi_load_from_memory(rawBytes.data, rawBytes.size, (int *)&metadata.width,
                                                   (int *)&metadata.height, (int *)&metadata.channels, 4);
        ASSERT(pixelData, "stbi_load failed for '" PRIu64 "': %s", metadata.guid, stbi_failure_reason());
        ASSERT(metadata.channels == 4, "Unexpected channels: %d", metadata.channels);

        const rhi_texture texture = Rhi::CreateTexture(rhi_texture_desc{
            .width = metadata.width,
            .height = metadata.height,
            .memoryUsage = rhi_memory_usage::GpuOnly,
            .usage = rhi_texture_usage::TransferDst | rhi_texture_usage::ShaderSampled,
            .format = rhi_texture_format::R8G8B8A8_sRGB,
        });
        metadata.texture = texture;

        const rhi_stv textureView = Rhi::CreateSampledTextureView(rhi_texture_view_desc{
            .texture = texture,
        });
        metadata.textureView = textureView;

        Rhi::CmdTransitionTexture(cmd, texture, rhi_texture_state::TransferDst);

        const uint32_t byteSize = metadata.width * metadata.height * metadata.channels;

        char *uploadMemory = GpuUpload::CmdCopyTexture(cmd, texture, byteSize);
        MemCpy(uploadMemory, pixelData, byteSize);

        free(pixelData);

        // TODO: this is suboptimal
        Rhi::CmdTransitionTexture(cmd, texture, rhi_texture_state::ShaderRead);
        metadata.state = texture_state::Uploaded;
    }
}

auto API DeclareTexture(byteview assetFileData, uint64_t guid) -> texture
{
    return HandlePool::Acquire(manager->textures, texture_metadata{
                                                      .guid = guid,
                                                      .state = texture_state::NotUploaded,
                                                  });
}

} // namespace TextureManager

} // namespace nyla