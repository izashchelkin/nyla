#pragma once

#include <cstdint>

#include "nyla/commons/word.h"

namespace nyla
{

struct asset_meta_header
{
    uint64_t guid;
    uint32_t entryCount;
};

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

} // namespace nyla