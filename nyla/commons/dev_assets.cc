#include "nyla/commons/dev_assets.h"

#include <cinttypes>
#include <cstdint>

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/asset_import.h"
#include "nyla/commons/asset_manager.h"
#include "nyla/commons/byteliterals.h"
#include "nyla/commons/byteparser.h"
#include "nyla/commons/dir_watcher.h"
#include "nyla/commons/file.h"
#include "nyla/commons/file_utils.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/mempage_pool.h"
#include "nyla/commons/platform_dir_watch.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/span.h"
#include "nyla/commons/span_def.h"
#include "nyla/commons/stringparser.h"
#include "nyla/commons/tokenparser.h"

namespace nyla
{

namespace
{

struct dev_asset_entry
{
    byteview dirPath;
    byteview name;
    uint64_t guid;
    span<uint8_t> slot;
};

constexpr inline uint64_t kSpvSlotSize = 256_KiB;

struct dev_assets_state
{
    region_alloc persistent;
    region_alloc scratch;
    inline_vec<dev_asset_entry, 1024> entries;
};

dev_assets_state *g_dev;

auto CopyByteview(region_alloc &alloc, byteview src) -> byteview
{
    span<uint8_t> dst = RegionAlloc::AllocArray<uint8_t>(alloc, src.size + 1);
    MemCpy(dst.data, src.data, src.size);
    dst.data[src.size] = 0;
    return byteview{dst.data, src.size};
}

auto ParseGuidFromMeta(byteview metaContents) -> uint64_t
{
    byte_parser p;
    ByteParser::Init(p, metaContents.data, metaContents.size);

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
            return TokenParser::ParseHexU64(p);

        ByteParser::NextLine(p);
    }
    return 0;
}

auto FindEntry(byteview dirPath, byteview name) -> dev_asset_entry *
{
    for (uint64_t i = 0; i < g_dev->entries.size; ++i)
    {
        dev_asset_entry &e = g_dev->entries[i];
        if (Span::Eq(e.dirPath, dirPath) && Span::Eq(e.name, name))
            return &e;
    }
    return nullptr;
}

auto LookupAndOpen(const dir_watcher_event &ev, dev_asset_entry *&outEntry, byteview &outFullPath) -> file_handle
{
    if (!Any(ev.mask & (platform_dir_watch_event_type::Modified | platform_dir_watch_event_type::MovedTo)))
        return nullptr;

    dev_asset_entry *entry = FindEntry(ev.dirPath, ev.name);
    if (!entry)
    {
        LOG("dev_assets: no guid for " SV_FMT "/" SV_FMT, SV_ARG(ev.dirPath), SV_ARG(ev.name));
        return nullptr;
    }

    auto &fullPath = RegionAlloc::AllocVec<uint8_t, 0x200>(g_dev->scratch);
    InlineVec::Append(fullPath, entry->dirPath);
    InlineVec::Append(fullPath, "/"_s);
    InlineVec::Append(fullPath, entry->name);
    InlineVec::Append(fullPath, byteview{(const uint8_t *)"\0", 1});
    fullPath.size -= 1;

    file_handle file = FileOpen(fullPath, FileOpenMode::Read);
    if (!FileValid(file))
    {
        LOG("dev_assets: open failed " SV_FMT, SV_ARG((byteview)fullPath));
        return nullptr;
    }

    outEntry = entry;
    outFullPath = fullPath;
    return file;
}

void OnSpvEvent(const dir_watcher_event &ev, void *)
{
    RegionAlloc::Reset(g_dev->scratch);

    dev_asset_entry *entry = nullptr;
    byteview fullPath{};
    file_handle file = LookupAndOpen(ev, entry, fullPath);
    if (!file)
        return;

    FileSeek(file, 0, file_seek_mode::End);
    uint64_t fileSize = FileTell(file);
    FileSeek(file, 0, file_seek_mode::Begin);

    if (entry->slot.data == nullptr)
        entry->slot = RegionAlloc::AllocArray<uint8_t>(g_dev->persistent, kSpvSlotSize);

    if (fileSize > entry->slot.size)
    {
        LOG("dev_assets: file too big for slot " SV_FMT " (%" PRIu64 " > %" PRIu64 ")", SV_ARG(fullPath), fileSize,
            entry->slot.size);
        FileClose(file);
        return;
    }

    uint64_t read = 0;
    uint64_t remaining = fileSize;
    uint32_t n;
    while (remaining > 0 && (n = FileRead(file, remaining, entry->slot.data + read)))
    {
        read += n;
        remaining -= n;
    }
    FileClose(file);
    if (remaining != 0)
    {
        LOG("dev_assets: read failed " SV_FMT, SV_ARG(fullPath));
        return;
    }

    byteview bytes{entry->slot.data, fileSize};
    AssetManager::Set(entry->guid, bytes);
    LOG("dev_assets: reloaded 0x%016" PRIx64 " " SV_FMT "/" SV_FMT " (%" PRIu64 " bytes)", entry->guid,
        SV_ARG(entry->dirPath), SV_ARG(entry->name), bytes.size);
}

void OnTextureEvent(const dir_watcher_event &ev, void *)
{
    RegionAlloc::Reset(g_dev->scratch);

    dev_asset_entry *entry = nullptr;
    byteview fullPath{};
    file_handle file = LookupAndOpen(ev, entry, fullPath);
    if (!file)
        return;

    span<uint8_t> raw;
    bool readOk = TryFileReadFully(g_dev->scratch, file, raw);
    FileClose(file);
    if (!readOk)
    {
        LOG("dev_assets: read failed " SV_FMT, SV_ARG(fullPath));
        return;
    }

    byteview blob = ImportTextureFromPngOrJpg(byteview{raw.data, raw.size}, g_dev->persistent);
    if (blob.size == 0)
    {
        LOG("dev_assets: image decode failed " SV_FMT, SV_ARG(fullPath));
        return;
    }

    AssetManager::Set(entry->guid, blob);
    LOG("dev_assets: reloaded 0x%016" PRIx64 " " SV_FMT "/" SV_FMT " (%" PRIu64 " bytes)", entry->guid,
        SV_ARG(entry->dirPath), SV_ARG(entry->name), blob.size);
}

// Raw passthrough — copy file bytes into persistent and route through AssetManager.
// Used for asset types whose packed and source layout match (gltf, bin, wav, bdf).
// Each reload allocates fresh from persistent — bounded by edits per session, OK for
// dev sessions; promote to per-guid slots if memory growth becomes a problem.
void OnRawAssetEvent(const dir_watcher_event &ev, void *)
{
    RegionAlloc::Reset(g_dev->scratch);

    dev_asset_entry *entry = nullptr;
    byteview fullPath{};
    file_handle file = LookupAndOpen(ev, entry, fullPath);
    if (!file)
        return;

    FileSeek(file, 0, file_seek_mode::End);
    uint64_t fileSize = FileTell(file);
    FileSeek(file, 0, file_seek_mode::Begin);

    span<uint8_t> dst = RegionAlloc::AllocArray<uint8_t>(g_dev->persistent, fileSize);

    uint64_t read = 0;
    uint64_t remaining = fileSize;
    uint32_t n;
    while (remaining > 0 && (n = FileRead(file, remaining, dst.data + read)))
    {
        read += n;
        remaining -= n;
    }
    FileClose(file);
    if (remaining != 0)
    {
        LOG("dev_assets: read failed " SV_FMT, SV_ARG(fullPath));
        return;
    }

    byteview bytes{dst.data, fileSize};
    AssetManager::Set(entry->guid, bytes);
    LOG("dev_assets: reloaded 0x%016" PRIx64 " " SV_FMT "/" SV_FMT " (%" PRIu64 " bytes)", entry->guid,
        SV_ARG(entry->dirPath), SV_ARG(entry->name), bytes.size);
}

void ScanDir(byteview dirPath, bool watchThis)
{
    dir_iter *it = DirIter::Create(g_dev->scratch, dirPath);
    if (!it)
        return;

    bool watched = false;

    file_metadata meta;
    while (DirIter::Next(g_dev->scratch, *it, meta))
    {
        if (Any(meta.attributes & file_attribute::Hidden))
            continue;
        if (Span::Eq(meta.fileName, "."_s) || Span::Eq(meta.fileName, ".."_s))
            continue;

        if (Any(meta.attributes & file_attribute::Directory))
        {
            auto &subDir = RegionAlloc::AllocVec<uint8_t, 0x200>(g_dev->scratch);
            InlineVec::Append(subDir, dirPath);
            InlineVec::Append(subDir, "/"_s);
            InlineVec::Append(subDir, meta.fileName);
            ScanDir(subDir, true);
            continue;
        }

        if (!Span::EndsWith(meta.fileName, ".meta"_s))
            continue;

        byteview assetName{meta.fileName.data, meta.fileName.size - 5};
        if (assetName.size == 0)
            continue;

        auto &metaPath = RegionAlloc::AllocVec<uint8_t, 0x200>(g_dev->scratch);
        InlineVec::Append(metaPath, dirPath);
        InlineVec::Append(metaPath, "/"_s);
        InlineVec::Append(metaPath, meta.fileName);

        file_handle f = FileOpen(metaPath, FileOpenMode::Read);
        if (!FileValid(f))
            continue;

        span<uint8_t> contents = FileReadFully(g_dev->scratch, f);
        FileClose(f);

        uint64_t guid = ParseGuidFromMeta(contents);
        if (!guid)
            continue;

        InlineVec::Append(g_dev->entries, dev_asset_entry{
                                              .dirPath = CopyByteview(g_dev->persistent, dirPath),
                                              .name = CopyByteview(g_dev->persistent, assetName),
                                              .guid = guid,
                                          });
        watched = true;
    }

    DirIter::Destroy(*it);

    if (watchThis && watched)
        DirWatcher::WatchDir(g_dev->persistent, dirPath);
}

} // namespace

namespace DevAssets
{

void API Bootstrap(span<const byteview> roots)
{
    g_dev = &RegionAlloc::Alloc<dev_assets_state>(RegionAlloc::g_BootstrapAlloc);
    g_dev->persistent = RegionAlloc::Create(MemPagePool::kChunkSize, 0);
    g_dev->scratch = RegionAlloc::Create(MemPagePool::kChunkSize, 0);

    DirWatcher::Subscribe(".spv"_s, OnSpvEvent, nullptr);
    DirWatcher::Subscribe(".png"_s, OnTextureEvent, nullptr);
    DirWatcher::Subscribe(".jpg"_s, OnTextureEvent, nullptr);
    DirWatcher::Subscribe(".gltf"_s, OnRawAssetEvent, nullptr);
    DirWatcher::Subscribe(".bin"_s, OnRawAssetEvent, nullptr);
    DirWatcher::Subscribe(".wav"_s, OnRawAssetEvent, nullptr);
    DirWatcher::Subscribe(".bdf"_s, OnRawAssetEvent, nullptr);
    DirWatcher::Subscribe(".pipeline"_s, OnRawAssetEvent, nullptr);

    for (uint64_t i = 0; i < roots.size; ++i)
        ScanDir(roots[i], true);

    RegionAlloc::Reset(g_dev->scratch);

    LOG("dev_assets: %" PRIu64 " entries indexed", g_dev->entries.size);
}

} // namespace DevAssets

} // namespace nyla
