#pragma once

#include "nyla/commons/handle.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

struct mesh : handle
{
};

namespace MeshManager
{

void API Bootstrap();

void API Update(region_alloc &alloc, rhi_cmdlist cmd, byteview assetFile);

auto API DeclareTexture(byteview assetFileData, uint64_t guidGltf, uint64_t guidBin) -> mesh;

void API CmdBindMesh(rhi_cmdlist cmd, mesh Mesh);
void API CmdDrawMesh(rhi_cmdlist cmd, mesh Mesh);

} // namespace MeshManager

} // namespace nyla
