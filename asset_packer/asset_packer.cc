#include <cinttypes>
#include <cstdint>

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/asset_file_format.h"
#include "nyla/commons/asset_manager.h"
#include "nyla/commons/bdf.h"
#include "nyla/commons/cast.h"
#include "nyla/commons/color.h"
#include "nyla/commons/debug_text_renderer.h"
#include "nyla/commons/engine.h"
#include "nyla/commons/entrypoint.h"
#include "nyla/commons/file.h"
#include "nyla/commons/file_utils.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/gamepad.h"
#include "nyla/commons/glyph_renderer.h"
#include "nyla/commons/gpu_upload.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/input_manager.h"
#include "nyla/commons/keyboard.h"
#include "nyla/commons/lerp.h"
#include "nyla/commons/limits.h"
#include "nyla/commons/macros.h" // IWYU pragma: keep
#include "nyla/commons/mat.h"
#include "nyla/commons/math.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/mempage_pool.h"
#include "nyla/commons/mesh_manager.h"
#include "nyla/commons/minmax.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/random.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/render_targets.h"
#include "nyla/commons/renderer.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/sampler_manager.h"
#include "nyla/commons/span_def.h"
#include "nyla/commons/spv_shader_enums.h"
#include "nyla/commons/texture_manager.h"
#include "nyla/commons/time.h"
#include "nyla/commons/tween_manager.h"
#include "nyla/commons/vec.h"

#define STB_IMAGE_IMPLEMENTATION
// #define STBI_NO_JPEG
// #define STBI_NO_PNG
#define STBI_NO_BMP
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "asset_packer/stb_image.h"

namespace nyla
{

enum class AssetType
{
    Unknown = 0,
    Texture = 1,
    Gltf = 2,
    Bin = 3,
    BdfFont = 4,
};

void UserMain()
{
    uint64_t randomState[4];
    SeedXoshiro256ss(randomState);

    file_handle output = FileOpen("test.bin"_s, FileOpenMode::Write | FileOpenMode::Append);
    ASSERT(FileValid(output));

    auto alloc = RegionAlloc::Create(MemPagePool::kChunkSize, 0);

    auto &searchList = RegionAlloc::AllocVec<byteview, 256>(alloc);
    InlineVec::Append(searchList, R"(assets)"_s);

    while (searchList.size > 0)
    {
        byteview currentDir = InlineVec::PopBack(searchList);

        file_metadata meta;
        dir_iter *dirIter = DirIter::Create(alloc, currentDir);
        if (dirIter)
        {
            while (DirIter::Next(alloc, *dirIter, meta))
            {
                if (Any(meta.attributes & file_attribute::Hidden))
                    continue;
                if (Span::Eq(meta.fileName, "."_s))
                    continue;
                if (Span::Eq(meta.fileName, ".."_s))
                    continue;

                if (Any(meta.attributes & file_attribute::Directory))
                {
                    auto &subDir = RegionAlloc::AllocVec<uint8_t, 0x100>(alloc);
                    InlineVec::Append(subDir, currentDir);
                    InlineVec::Append(subDir, "/"_s);
                    InlineVec::Append(subDir, meta.fileName);

                    InlineVec::Append(searchList, subDir);
                }
                else
                {
                    AssetType type;
                    if (Span::EndsWith(meta.fileName, ".png"_s))
                        type = AssetType::Texture;
                    else if (Span::EndsWith(meta.fileName, ".jpg"_s))
                        type = AssetType::Texture;
                    else if (Span::EndsWith(meta.fileName, ".bdf"_s))
                        type = AssetType::BdfFont;
                    else if (Span::EndsWith(meta.fileName, ".gltf"_s))
                        type = AssetType::Gltf;
                    else if (Span::EndsWith(meta.fileName, ".bin"_s))
                        type = AssetType::Bin;
                    else
                        type = AssetType::Unknown;

                    if (type != AssetType::Unknown)
                    {
                        void *allocMark = alloc.at;

                        auto &fullPath = RegionAlloc::AllocVec<uint8_t, 0x100>(alloc);
                        InlineVec::Append(fullPath, currentDir);
                        InlineVec::Append(fullPath, "/"_s);
                        InlineVec::Append(fullPath, meta.fileName);

                        auto &metaPath = RegionAlloc::AllocVec<uint8_t, 0x100>(alloc);
                        InlineVec::Append(metaPath, (byteview)fullPath);
                        InlineVec::Append(metaPath, ".meta"_s);

                        file_handle metaFile = FileOpen(metaPath, FileOpenMode::Read | FileOpenMode::Append);
                        ASSERT(FileValid(metaFile));

                        uint64_t metaFileSize = FileTell(metaFile);
                        if (FileTell(metaFile) > 0)
                        {
                            LOG("already written!");

                            FileSeek(metaFile, 0, file_seek_mode::Begin);

                            span buf = RegionAlloc::AllocArray<uint8_t>(alloc, metaFileSize);
                            FileRead(metaFile, buf.size, buf.data);

                            asset_meta_header *header = (asset_meta_header *)buf.data;

                            LOG("guid: %" PRIu64 " entryCount: %" PRIu64, header->guid, header->entryCount);
                        }
                        else
                        {
                            LOG("writing...");

                            uint64_t guid = Xoshiro256ss(randomState);
                            FileWrite(metaFile, asset_meta_header{
                                                    .guid = guid,
                                                    .entryCount = 0,
                                                });
                        }

                        file_handle file = FileOpen(fullPath, FileOpenMode::Read);
                        ASSERT(FileValid(file));

                        LOG("file: " SV_FMT, SV_ARG(fullPath));

                        span rawBytes = FileReadFully(alloc, file);

                        switch (type)
                        {
                        case AssetType::Texture: {
                            int texWidth;
                            int texHeight;

                            {
                                uint8_t *pixelData = stbi_load_from_memory(rawBytes.data, CastI32(rawBytes.size),
                                                                           &texWidth, &texHeight, nullptr, 4);
#if 1
                                ASSERT(pixelData);
#else
                                ASSERT(pixelData, "stbi_load failed for '%" PRIu64 "': %s", metadata.guid,
                                       stbi_failure_reason());
#endif

                                uint32_t pixelDataSize = (uint32_t)4 * texWidth * texHeight;

                                FileWrite(output, pixelDataSize, pixelData);
                            }
                            break;
                        }
                        }

                        FileClose(file);
                        RegionAlloc::Reset(alloc, allocMark);
                    }
                }
            };

            DirIter::Destroy(*dirIter);
        }
    }
}

} // namespace nyla