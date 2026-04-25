#include "nyla/commons/asset_manager.h"

#include <cstdint>

#include "nyla/commons/align.h"
#include "nyla/commons/array.h"
#include "nyla/commons/file.h"
#include "nyla/commons/file_utils.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mempage_pool.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/word.h"

namespace nyla
{

namespace
{

constexpr inline uint32_t kAssetFileMagic = DWord("SBA ");

struct asset_file_header
{
    uint32_t magic;
    uint32_t fileCount;
};

struct asset_file_index_entry
{
    uint64_t dataOffset;
    uint64_t dataSize;
    uint64_t timestamp;
    uint64_t guid;
    uint32_t crc32;
    uint32_t pathLength;
};

struct asset_manager
{
    byteview assetFile;
    array<byteview, 0x100> dynamicResources;
};
asset_manager *manager;

} // namespace

namespace AssetManager
{

void API Bootstrap(file_handle assetFile)
{
    manager = &RegionAlloc::Alloc<asset_manager>(RegionAlloc::g_BootstrapAlloc);

    region_alloc alloc = RegionAlloc::Create(MemPagePool::kChunkSize, FileTell(assetFile));
    manager->assetFile = FileReadFully(alloc, assetFile);

    ASSERT(*(uint32_t *)manager->assetFile.data == kAssetFileMagic);
}

void API Set(uint64_t guid, byteview data)
{
    ASSERT(guid && guid < 0x100);

    manager->dynamicResources[guid] = data;
}

auto API Get(uint64_t guid) -> byteview
{
    ASSERT(guid);

    if (guid < 0x100)
        return manager->dynamicResources[guid];

    uint8_t *ptr = (uint8_t *)manager->assetFile.data;
    ptr += 4;

    uint32_t fileCount = *ptr;
    ptr += 4;

    for (uint32_t i = 0; i < fileCount; ++i)
    {
        asset_file_index_entry *ent = (asset_file_index_entry *)ptr;

        if (ent->guid == guid)
            return {manager->assetFile.data + ent->dataOffset, ent->dataSize};

        ptr += sizeof(asset_file_index_entry);
        ptr += ent->pathLength;
        ptr = AlignedUp(ptr, 8);
    }

    TRAP();
    UNREACHABLE();
}

} // namespace AssetManager

} // namespace nyla