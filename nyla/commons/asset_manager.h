#pragma once

#include <cstdint>

#include "nyla/commons/file.h"
#include "nyla/commons/macros.h"

namespace nyla
{

using asset_subscriber = void (*)(uint64_t guid, byteview data, void *user);

namespace AssetManager
{

void API Bootstrap(file_handle assetFile);
void API Set(uint64_t guid, byteview data);
auto API Get(uint64_t guid) -> byteview;
void API Subscribe(asset_subscriber cb, void *user);

} // namespace AssetManager

} // namespace nyla
