#include "nyla/commons/asset_manager.h"

#include <cstdint>

#include "nyla/commons/array.h"
#include "nyla/commons/asset_file_format.h"
#include "nyla/commons/binary_search.h"
#include "nyla/commons/file.h"
#include "nyla/commons/file_utils.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mempage_pool.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

namespace
{

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

    ASSERT(manager->assetFile.size >= sizeof(assetdb_header));
    auto *header = (const assetdb_header *)manager->assetFile.data;
    ASSERT(header->magic == kAssetDbMagic);
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

    auto *header = (const assetdb_header *)manager->assetFile.data;
    auto *index = (const assetdb_index_entry *)(manager->assetFile.data + sizeof(assetdb_header));

    span<const assetdb_index_entry> indexSpan{index, header->entryCount};
    const assetdb_index_entry *ent =
        BinarySearch::Find(indexSpan, guid, [](const assetdb_index_entry &e) { return e.guid; });

    ASSERT(ent);
    return {manager->assetFile.data + ent->dataOffset, ent->dataSize};
}

} // namespace AssetManager

} // namespace nyla