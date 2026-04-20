#pragma once

#include "nyla/commons/handle.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/rhi.h"

namespace nyla
{

struct texture_handle : handle
{
};

namespace TextureManager
{

void API Bootstrap();

void API Update(rhi_cmdlist cmd, byteview assetFile);

auto API DeclareTexture(byteview assetFileData, uint64_t guid) -> texture_handle;

auto API GetSRV(texture_handle Texture) -> rhi_srv;

} // namespace TextureManager

} // namespace nyla