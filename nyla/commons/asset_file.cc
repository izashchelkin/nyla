#include "nyla/commons/asset_file.h"

#include <cstdint>

#include "nyla/commons/align.h"
#include "nyla/commons/file.h"
#include "nyla/commons/file_utils.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mempage_pool.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/span.h"

namespace nyla
{

auto API AssetFileLoad(file_handle file) -> byteview
{
    region_alloc alloc = RegionAlloc::Create(MemPagePool::kChunkSize, FileTell(file));
    span<uint8_t> data = FileReadFully(alloc, file);

    ASSERT(*(uint32_t *)data.data == kAssetFileMagic);

    return data;
}

auto API AssetFileGetData(byteview assetFileData, byteview path) -> byteview
{
    uint8_t *ptr = (uint8_t *)assetFileData.data;
    ptr += 4;

    uint32_t fileCount = *ptr;
    ptr += 4;

    for (uint32_t i = 0; i < fileCount; ++i)
    {
        AssetFileIndexEntry *ent = (AssetFileIndexEntry *)ptr;

        if (Span::Eq(byteview{(uint8_t *)(ent + 1), ent->pathLength}, path))
            return {assetFileData.data + ent->dataOffset, ent->dataSize};

        ptr += sizeof(AssetFileIndexEntry);
        ptr += ent->pathLength;
        ptr = AlignedUp(ptr, 8);
    }

    TRAP();
    UNREACHABLE();
}

} // namespace nyla