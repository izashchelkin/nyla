#include "nyla/commons/asset_manager.h"

#include <cstdint>

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/asset_file_format.h"
#include "nyla/commons/binary_search.h"
#include "nyla/commons/file.h"
#include "nyla/commons/file_utils.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mempage_pool.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

namespace
{

struct override_entry
{
    uint64_t guid;
    byteview data;
};

struct subscriber_entry
{
    asset_subscriber cb;
    void *user;
};

struct asset_manager
{
    byteview assetFile;
    array<byteview, 0x100> dynamicResources;
    inline_vec<override_entry, 256> overrides;
    inline_vec<subscriber_entry, 16> subscribers;
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
    ASSERT(guid);

    if (guid < 0x100)
    {
        manager->dynamicResources[guid] = data;
    }
    else
    {
        bool replaced = false;
        for (uint64_t i = 0; i < manager->overrides.size; ++i)
        {
            if (manager->overrides[i].guid == guid)
            {
                manager->overrides[i].data = data;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            InlineVec::Append(manager->overrides, override_entry{.guid = guid, .data = data});
    }

    for (uint64_t i = 0; i < manager->subscribers.size; ++i)
        manager->subscribers[i].cb(guid, data, manager->subscribers[i].user);
}

void API Subscribe(asset_subscriber cb, void *user)
{
    InlineVec::Append(manager->subscribers, subscriber_entry{.cb = cb, .user = user});
}

auto API Get(uint64_t guid) -> byteview
{
    ASSERT(guid);

    if (guid < 0x100)
        return manager->dynamicResources[guid];

    for (uint64_t i = 0; i < manager->overrides.size; ++i)
    {
        if (manager->overrides[i].guid == guid)
            return manager->overrides[i].data;
    }

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