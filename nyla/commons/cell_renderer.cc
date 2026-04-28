#include "nyla/commons/cell_renderer.h"

#include <cstdint>

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/asset_file_format.h"
#include "nyla/commons/asset_manager.h"
#include "nyla/commons/bdf.h"
#include "nyla/commons/byteliterals.h"
#include "nyla/commons/byteparser.h"
#include "nyla/commons/limits.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/pipeline_cache.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/sampler_manager.h"
#include "nyla/commons/span.h"
#include "nyla/commons/span_def.h"
#include "nyla/commons/texture_manager.h"

namespace nyla
{

namespace
{

constexpr uint64_t kInternalAtlasGuid = 0xC11AB0BCE11A71A5;
constexpr uint16_t kCodepointMapSize = 256;
constexpr uint16_t kInvalidGlyph = 0xFFFFu;

struct gpu_cell
{
    uint32_t packedXY;    // (cellY << 16) | cellX
    uint32_t packedGlyph; // (flags << 16) | glyphIndex
    uint32_t fgRgba;
    uint32_t bgRgba;
};

struct cell_renderer_state
{
    cell_renderer_init_desc desc;
    uint32_t atlasPxW;
    uint32_t atlasPxH;

    texture_handle atlasTex;
    pipeline_cache_handle pipeline;

    rhi_buffer instanceBuffer;
    uint32_t bytesPerFrame;

    uint16_t codepointToGlyph[kCodepointMapSize];

    int32_t originPxX;
    int32_t originPxY;
    uint32_t cols;
    uint32_t rows;
    uint32_t cellCount;
    uint32_t cellCap;
    gpu_cell *frameCells;

    uint32_t lastFrameIdx;
    uint32_t frameSliceByteOffset;
    uint32_t currentDrawByteOffset;
};

cell_renderer_state *cr;

void BuildAtlas(byteview bdfData, uint8_t *rgbaPixels)
{
    bdf_parser parser{};
    ByteParser::Init(parser, bdfData.data, bdfData.size);

    region_alloc tmp = RegionAlloc::Create(8_MiB, 0);

    const uint32_t cellW = cr->desc.cellPxW;
    const uint32_t cellH = cr->desc.cellPxH;
    const uint32_t glyphsPerRow = cr->desc.atlasGlyphsPerRow;
    const uint32_t maxGlyphs = cr->desc.atlasGlyphsPerRow * cr->desc.atlasGlyphsPerCol;
    const uint32_t atlasW = cr->atlasPxW;

    void *allocMark = tmp.at;
    for (bdf_glyph glyph; BdfParser::NextGlyph(parser, tmp, glyph); RegionAlloc::Reset(tmp, allocMark))
    {
        if (glyph.encoding >= kCodepointMapSize)
            continue;
        if (glyph.encoding >= maxGlyphs)
            continue;

        const uint16_t glyphIdx = static_cast<uint16_t>(glyph.encoding);
        cr->codepointToGlyph[glyph.encoding] = glyphIdx;

        const uint32_t cellX = glyphIdx % glyphsPerRow;
        const uint32_t cellY = glyphIdx / glyphsPerRow;

        uint32_t ipixel = 0;
        for (uint32_t b : glyph.bitmap)
        {
            for (uint32_t i = 0; i < 8; ++i)
            {
                const uint32_t pixelX = ipixel % 16;
                const uint32_t pixelY = ipixel / 16;
                ++ipixel;

                if (pixelX >= cellW || pixelY >= cellH)
                    continue;

                const uint32_t x = cellX * cellW + pixelX;
                const uint32_t y = cellY * cellH + pixelY;
                const uint8_t mask = ((b >> (7u - i)) & 1u) * Limits<uint8_t>::Max();

                uint8_t *p = rgbaPixels + (uint64_t{y} * atlasW + x) * 4;
                p[0] = mask;
                p[1] = mask;
                p[2] = mask;
                p[3] = mask;
            }
        }
    }
    RegionAlloc::Destroy(tmp);
}

} // namespace

namespace CellRenderer
{

void API Bootstrap(region_alloc &, const cell_renderer_init_desc &desc)
{
    cr = &RegionAlloc::Alloc<cell_renderer_state>(RegionAlloc::g_BootstrapAlloc);
    cr->desc = desc;
    cr->atlasPxW = desc.atlasGlyphsPerRow * desc.cellPxW;
    cr->atlasPxH = desc.atlasGlyphsPerCol * desc.cellPxH;

    for (uint16_t &slot : cr->codepointToGlyph)
        slot = kInvalidGlyph;

    {
        const uint32_t pixelBytes = cr->atlasPxW * cr->atlasPxH * 4u;
        const uint32_t totalBytes = sizeof(texture_blob_header) + pixelBytes;

        uint8_t *blob = RegionAlloc::Alloc(RegionAlloc::g_BootstrapAlloc, totalBytes, alignof(texture_blob_header));
        auto *header = reinterpret_cast<texture_blob_header *>(blob);
        header->width = cr->atlasPxW;
        header->height = cr->atlasPxH;
        header->format = 0;
        header->pixelOffset = sizeof(texture_blob_header);

        uint8_t *pixels = blob + header->pixelOffset;

        byteview bdfData = AssetManager::Get(desc.bdfGuid);
        ASSERT(bdfData.size > 0);
        BuildAtlas(bdfData, pixels);

        AssetManager::Set(kInternalAtlasGuid, byteview{blob, totalBytes});
        cr->atlasTex = TextureManager::DeclareTexture(kInternalAtlasGuid);
    }

    cr->bytesPerFrame = desc.maxCells * sizeof(gpu_cell);
    cr->lastFrameIdx = ~0u;
    cr->frameSliceByteOffset = 0;
    const uint32_t numFrames = Rhi::GetNumFramesInFlight();

    cr->instanceBuffer = Rhi::CreateBuffer(rhi_buffer_desc{
        .size = uint64_t{cr->bytesPerFrame} * numFrames,
        .bufferUsage = rhi_buffer_usage::Vertex,
        .memoryUsage = rhi_memory_usage::CpuToGpu,
    });
    Rhi::NameBuffer(cr->instanceBuffer, "CellRendererInstances"_s);

    rhi_vertex_attribute_desc vertexAttribute{
        .binding = 0,
        .semantic = "ATTRIB0"_s,
        .format = rhi_vertex_format::R32G32B32A32Uint,
        .offset = 0,
    };
    rhi_vertex_binding_desc vertexBinding{
        .binding = 0,
        .stride = sizeof(gpu_cell),
        .inputRate = rhi_input_rate::PerInstance,
    };
    rhi_texture_format colorFormat = rhi_texture_format::B8G8R8A8_sRGB;

    const rhi_graphics_pipeline_desc pipelineDesc{
        .debugName = "CellRenderer"_s,
        .vertexBindings = {&vertexBinding, 1},
        .vertexAttributes = {&vertexAttribute, 1},
        .colorTargetFormats = {&colorFormat, 1},
        .depthFormat = rhi_texture_format::D32_Float_S8_UINT,
    };

    cr->pipeline = PipelineCache::Acquire(0x6D1A8F7C2E5B9043, 0x9E3F4D8A1C07B2E6, pipelineDesc);
}

void API Begin(int32_t originPxX, int32_t originPxY, uint32_t cols, uint32_t rows)
{
    cr->originPxX = originPxX;
    cr->originPxY = originPxY;
    cr->cols = cols;
    cr->rows = rows;
    cr->cellCount = 0;

    const uint32_t frameIdx = Rhi::GetFrameIndex();
    if (frameIdx != cr->lastFrameIdx)
    {
        cr->lastFrameIdx = frameIdx;
        cr->frameSliceByteOffset = 0;
    }

    cr->currentDrawByteOffset = cr->frameSliceByteOffset;

    const uint32_t bytesRemaining =
        cr->frameSliceByteOffset < cr->bytesPerFrame ? cr->bytesPerFrame - cr->frameSliceByteOffset : 0u;
    cr->cellCap = bytesRemaining / sizeof(gpu_cell);
    if (cr->cellCap > cr->desc.maxCells)
        cr->cellCap = cr->desc.maxCells;

    char *base = Rhi::MapBuffer(cr->instanceBuffer);
    cr->frameCells =
        reinterpret_cast<gpu_cell *>(base + uint64_t{frameIdx} * cr->bytesPerFrame + cr->currentDrawByteOffset);
}

auto API GlyphForCodepoint(uint32_t codepoint) -> uint16_t
{
    if (codepoint >= kCodepointMapSize)
        return kInvalidGlyph;
    return cr->codepointToGlyph[codepoint];
}

void API PutCell(uint32_t col, uint32_t row, cell_attr cell)
{
    if (col >= cr->cols || row >= cr->rows)
        return;
    if (cr->cellCount >= cr->cellCap)
        return;
    if (cell.glyphIndex == kInvalidGlyph)
        return;

    gpu_cell &gc = cr->frameCells[cr->cellCount++];
    gc.packedXY = (row << 16) | (col & 0xFFFFu);
    gc.packedGlyph = (uint32_t{cell.flags} << 16) | uint32_t{cell.glyphIndex};
    gc.fgRgba = cell.fgRgba;
    gc.bgRgba = cell.bgRgba;
}

void API Text(uint32_t col, uint32_t row, byteview text, uint32_t fgRgba, uint32_t bgRgba)
{
    for (uint64_t i = 0; i < text.size; ++i)
    {
        const uint32_t c = static_cast<uint32_t>(static_cast<uint8_t>(text.data[i]));
        const uint16_t glyph = GlyphForCodepoint(c);
        if (glyph == kInvalidGlyph)
            continue;

        cell_attr cell{
            .glyphIndex = glyph,
            .flags = 0,
            .fgRgba = fgRgba,
            .bgRgba = bgRgba,
        };
        PutCell(col + static_cast<uint32_t>(i), row, cell);
    }
}

void API CmdFlush(rhi_cmdlist cmd)
{
    if (cr->cellCount == 0)
        return;

    const uint32_t frameIdx = Rhi::GetFrameIndex();
    const uint32_t drawBytes = cr->cellCount * sizeof(gpu_cell);
    Rhi::BufferMarkWritten(cr->instanceBuffer, frameIdx * cr->bytesPerFrame + cr->currentDrawByteOffset, drawBytes);
    cr->frameSliceByteOffset = cr->currentDrawByteOffset + drawBytes;

    rhi_srv atlasSrv = TextureManager::GetSRV(cr->atlasTex);
    if (!atlasSrv)
        return;

    rhi_texture backbuffer = Rhi::GetTexture(Rhi::GetBackbufferView());
    rhi_texture_info backbufferInfo = Rhi::GetTextureInfo(backbuffer);

    struct PassConst
    {
        uint32_t screenW;
        uint32_t screenH;
        int32_t originX;
        int32_t originY;
        uint32_t cellW;
        uint32_t cellH;
        uint32_t atlasW;
        uint32_t atlasH;
        uint32_t atlasSrvIndex;
        uint32_t samplerIndex;
        uint32_t pad0;
        uint32_t pad1;
    } passConst{
        .screenW = backbufferInfo.width,
        .screenH = backbufferInfo.height,
        .originX = cr->originPxX,
        .originY = cr->originPxY,
        .cellW = cr->desc.cellPxW,
        .cellH = cr->desc.cellPxH,
        .atlasW = cr->atlasPxW,
        .atlasH = cr->atlasPxH,
        .atlasSrvIndex = atlasSrv.index,
        .samplerIndex = uint32_t(sampler_type::NearestClamp),
    };

    Rhi::CmdBindGraphicsPipeline(cmd, PipelineCache::Resolve(cr->pipeline));
    Rhi::SetLargeDrawConstant(cmd, Span::ByteViewPtr(&passConst));

    const uint64_t bufferOffset = uint64_t{frameIdx} * cr->bytesPerFrame + cr->currentDrawByteOffset;
    Rhi::CmdBindVertexBuffers(cmd, 0, {&cr->instanceBuffer, 1}, {&bufferOffset, 1});

    Rhi::CmdDraw(cmd, 6, cr->cellCount, 0, 0);
}

} // namespace CellRenderer

} // namespace nyla
