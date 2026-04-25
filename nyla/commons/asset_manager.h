#pragma once

#include <cstdint>

#include "nyla/commons/file.h"
#include "nyla/commons/macros.h"

namespace nyla
{

namespace AssetManager
{

void API Bootstrap(file_handle assetFile);
void API Set(uint64_t guid, byteview data);
auto API Get(uint64_t guid) -> byteview;

} // namespace AssetManager

} // namespace nyla