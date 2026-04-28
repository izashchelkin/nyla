#include <cinttypes>
#include <cstdint>

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/asset_file_format.h"
#include "nyla/commons/asset_manager.h"
#include "nyla/commons/bdf.h"
#include "nyla/commons/byteparser.h"
#include "nyla/commons/cast.h"
#include "nyla/commons/color.h"
#include "nyla/commons/debug_text_renderer.h"
#include "nyla/commons/engine.h"
#include "nyla/commons/entrypoint.h"
#include "nyla/commons/file.h"
#include "nyla/commons/file_utils.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/gamepad.h"
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
#include "nyla/commons/stringparser.h"
#include "nyla/commons/texture_manager.h"
#include "nyla/commons/time.h"
#include "nyla/commons/tokenparser.h"
#include "nyla/commons/tween_manager.h"
#include "nyla/commons/vec.h"

#include "nyla/commons/asset_import.h"

namespace nyla
{

enum class AssetType
{
    Unknown = 0,
    Texture = 1,
    Gltf = 2,
    Bin = 3,
    BdfFont = 4,
    Spv = 5,
    Wav = 6,
    Pipeline = 7,
};

namespace
{

struct pending_entry
{
    uint64_t guid;
    byteview alias;
    AssetType type;
    byteview processed;
};

} // namespace

void UserMain()
{
    uint64_t randomState[4];
    SeedXoshiro256ss(randomState);

    auto alloc = RegionAlloc::Create(MemPagePool::kChunkSize, 0);

    auto &searchList = RegionAlloc::AllocVec<byteview, 256>(alloc);
    InlineVec::Append(searchList, R"(assets)"_s);
    InlineVec::Append(searchList, R"(asset_public)"_s);

    auto &entries = RegionAlloc::AllocVec<pending_entry, 4096>(alloc);

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
                    else if (Span::EndsWith(meta.fileName, ".spv"_s))
                        type = AssetType::Spv;
                    else if (Span::EndsWith(meta.fileName, ".wav"_s))
                        type = AssetType::Wav;
                    else if (Span::EndsWith(meta.fileName, ".pipeline"_s))
                        type = AssetType::Pipeline;
                    else
                        type = AssetType::Unknown;

                    if (type != AssetType::Unknown)
                    {
                        auto &fullPathVec = RegionAlloc::AllocVec<uint8_t, 0x100>(alloc);
                        InlineVec::Append(fullPathVec, currentDir);
                        InlineVec::Append(fullPathVec, "/"_s);
                        InlineVec::Append(fullPathVec, meta.fileName);
                        byteview fullPath = fullPathVec;

                        auto &metaPathVec = RegionAlloc::AllocVec<uint8_t, 0x100>(alloc);
                        InlineVec::Append(metaPathVec, fullPath);
                        InlineVec::Append(metaPathVec, ".meta"_s);
                        byteview metaPath = metaPathVec;

                        uint64_t guid = 0;
                        byteview alias{};
                        file_handle metaFile = FileOpen(metaPath, FileOpenMode::Read);
                        if (FileValid(metaFile))
                        {
                            span buf = FileReadFully(alloc, metaFile);
                            FileClose(metaFile);

                            byte_parser p;
                            ByteParser::Init(p, buf.data, buf.size);
                            while (ByteParser::HasNext(p))
                            {
                                StringParser::SkipWhitespace(p);
                                if (!ByteParser::HasNext(p))
                                    break;
                                if (TokenParser::SkipLineComment(p))
                                    continue;

                                byteview key = TokenParser::ParseIdentifier(p);
                                TokenParser::SkipLineWhitespace(p);

                                if (Span::Eq(key, "guid"_s))
                                    guid = TokenParser::ParseHexU64(p);
                                else if (Span::Eq(key, "alias"_s))
                                    alias = TokenParser::ParseIdentifier(p);
                                else
                                    ByteParser::NextLine(p);
                            }

                            LOG("meta exists guid: 0x%016" PRIX64 " alias: " SV_FMT, guid, SV_ARG(alias));
                        }
                        else
                        {
                            guid = Xoshiro256ss(randomState);

                            file_handle newMeta = FileOpen(metaPath, FileOpenMode::Write);
                            ASSERT(FileValid(newMeta));
                            FileWriteFmt(newMeta, "guid  0x%016" PRIX64 "\n# alias <fill_me>\n"_s, guid);
                            FileClose(newMeta);

                            LOG("meta minted guid: 0x%016" PRIX64 " (alias missing — fill in " SV_FMT ")", guid,
                                SV_ARG(metaPath));
                        }

                        file_handle file = FileOpen(fullPath, FileOpenMode::Read);
                        ASSERT(FileValid(file));

                        LOG("file: " SV_FMT, SV_ARG(fullPath));

                        span rawBytes = FileReadFully(alloc, file);
                        FileClose(file);

                        byteview processed;
                        switch (type)
                        {
                        case AssetType::Texture: {
                            processed = ImportTextureFromPngOrJpg(rawBytes, alloc);
                            ASSERT(processed.size > 0);
                            break;
                        }
                        case AssetType::Bin:
                        case AssetType::Gltf:
                        case AssetType::BdfFont:
                        case AssetType::Spv:
                        case AssetType::Wav:
                        case AssetType::Pipeline:
                            processed = rawBytes;
                            break;
                        case AssetType::Unknown:
                            UNREACHABLE();
                        }

                        InlineVec::Append(entries, pending_entry{
                                                       .guid = guid,
                                                       .alias = alias,
                                                       .type = type,
                                                       .processed = processed,
                                                   });
                    }
                }
            };

            DirIter::Destroy(*dirIter);
        }
    }

    file_handle assetsHeaderFile = FileOpen("assets.h"_s, FileOpenMode::Write);
    ASSERT(FileValid(assetsHeaderFile));

    FileWriteFmt(assetsHeaderFile, "#pragma once\n\n"_s);
    FileWriteFmt(assetsHeaderFile, "#include <cstdint>\n\n"_s);
    FileWriteFmt(assetsHeaderFile, "namespace nyla\n"_s);
    FileWriteFmt(assetsHeaderFile, "{\n\n"_s);

    for (uint64_t i = 0; i < entries.size; ++i)
    {
        auto &entry = entries[i];
        if (entry.alias.size)
        {
            for (uint64_t j = i + 1; j < entries.size; ++j)
                ASSERT(!Span::Eq(entry.alias, entries[j].alias), "duplicate alias");

            FileWriteFmt(assetsHeaderFile, "constexpr inline uint64_t ID_" SV_FMT " = 0x%" PRIx64 ";\n"_s,
                         SV_ARG(entry.alias), entry.guid);
        }
        else
        {
            LOG("missing alias in meta");
        }
    }

    FileWriteFmt(assetsHeaderFile, "\n} // namespace nyla"_s);

    for (uint64_t i = 1; i < entries.size; ++i)
    {
        pending_entry tmp = entries[i];
        uint64_t j = i;
        while (j > 0 && entries[j - 1].guid > tmp.guid)
        {
            entries[j] = entries[j - 1];
            --j;
        }
        entries[j] = tmp;
    }

    uint64_t entryCount = entries.size;
    uint64_t indexBytes = entryCount * sizeof(assetdb_index_entry);
    uint64_t dataAreaStart = AlignedUp(sizeof(assetdb_header) + indexBytes, 8);

    span<assetdb_index_entry> index = RegionAlloc::AllocArray<assetdb_index_entry>(alloc, entryCount);
    uint64_t cursor = dataAreaStart;
    for (uint64_t i = 0; i < entryCount; ++i)
    {
        cursor = AlignedUp(cursor, 8);
        index[i] = assetdb_index_entry{
            .guid = entries[i].guid,
            .dataOffset = cursor,
            .dataSize = entries[i].processed.size,
        };
        cursor += entries[i].processed.size;
    }

    file_handle output = FileOpen("assets.bin"_s, FileOpenMode::Write);
    ASSERT(FileValid(output));

    FileWrite(output, assetdb_header{
                          .magic = kAssetDbMagic,
                          .entryCount = (uint32_t)entryCount,
                      });
    if (entryCount > 0)
        FileWriteSpan(output, index);

    static const uint8_t kZeroPad[8] = {};
    uint64_t bytesWritten = sizeof(assetdb_header) + indexBytes;
    for (uint64_t i = 0; i < entryCount; ++i)
    {
        uint64_t targetOffset = index[i].dataOffset;
        if (targetOffset > bytesWritten)
        {
            uint64_t pad = targetOffset - bytesWritten;
            ASSERT(pad < 8);
            FileWrite(output, (uint32_t)pad, kZeroPad);
            bytesWritten += pad;
        }
        ASSERT(entries[i].processed.size <= UINT32_MAX);
        FileWrite(output, (uint32_t)entries[i].processed.size, entries[i].processed.data);
        bytesWritten += entries[i].processed.size;
    }

    FileClose(output);

    LOG("packed %" PRIu64 " entries to assets.bin", entryCount);
}

} // namespace nyla