#pragma once

#include "nyla/commons/handle.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/texture_manager.h"

namespace nyla
{

struct mesh_handle : handle
{
};

namespace MeshManager
{

void API Bootstrap();

void API Update(region_alloc &alloc, rhi_cmdlist cmd);

auto API DeclareMesh(uint64_t guidGltf, uint64_t guidBin) -> mesh_handle;

void API CmdBindMesh(rhi_cmdlist cmd, mesh_handle Mesh);
void API CmdDrawMesh(rhi_cmdlist cmd, mesh_handle Mesh);

auto API GetTexture(mesh_handle Mesh) -> texture_handle;

} // namespace MeshManager

} // namespace nyla