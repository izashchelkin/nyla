#pragma once

#include <cstdint>

#include "nyla/commons/file.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/word.h"

namespace nyla
{

constexpr inline uint32_t kAssetFileMagic = DWord("SBA ");

struct AssetFileHeader
{
    uint32_t magic;
    uint32_t fileCount;
};

struct AssetFileIndexEntry
{
    uint64_t dataOffset;
    uint64_t dataSize;
    uint64_t timestamp;
    uint64_t crc32;
    uint64_t pathLength;
};

auto API AssetFileLoad(file_handle file) -> byteview;
auto API AssetFileGetData(byteview assetFileData, byteview path) -> byteview;

} // namespace nyla