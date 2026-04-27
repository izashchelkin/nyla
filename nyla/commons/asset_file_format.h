#pragma once

#include <cstdint>

#include "nyla/commons/word.h"

namespace nyla
{

constexpr inline uint32_t kAssetDbMagic = DWord("ASDB");

struct assetdb_header
{
    uint32_t magic; // ASDB
    uint32_t entryCount;
};

struct assetdb_index_entry
{
    uint64_t guid;
    uint64_t dataOffset;
    uint64_t dataSize;
};

struct texture_blob_header
{
    uint32_t width;
    uint32_t height;
    uint32_t format; // 0 = RGBA8
    uint32_t pixelOffset;
};

} // namespace nyla