#include <cstdint>

#include "nyla/commons/asset_manager.h"
#include "nyla/commons/glyph_renderer.h"
#include "nyla/commons/rhi.h"

namespace nyla
{

namespace
{
RhiTexture g_AtlasTexture;
}

void GlyphRenderer::Init(uint8_t *atlas, uint32_t width, uint32_t height)
{

    // we should go via asset manager here

#if 0
    g_AtlasTexture = Rhi::CreateTexture(RhiTextureDesc{
        .width = width,
        .height = height,
        .memoryUsage = RhiMemoryUsage::GpuOnly,
        .usage = RhiTextureUsage::ShaderSampled | RhiTextureUsage::TransferDst,
        .format = RhiTextureFormat::R8_UNORM,
    });

    GpuUploadManager::CmdCopyTexture(RhiCmdList cmd, RhiTexture dst, uint64_t size);
#endif

#if 0
    AssetManager::DeclareTexture(Str path); // TODO: we need a way to provide texture data here
                                                         //
#endif
}

void GlyphRenderer::CmdFlush()
{
}

} // namespace nyla
