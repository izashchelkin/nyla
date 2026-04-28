#pragma once

#include <cstdint>

#include "nyla/commons/handle.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/rhi.h"

namespace nyla
{

struct pipeline_cache_handle : handle
{
};

namespace PipelineCache
{

void API Bootstrap();

// Acquire a pipeline. If stateGuid is nonzero, the runtime overrides depth/cull/front-face
// state from the asset bytes (parsed as a small text key=value file) and rebuilds the
// pipeline whenever those bytes change. stateGuid == 0 keeps the desc fields verbatim.
auto API Acquire(uint64_t vsGuid, uint64_t psGuid, const rhi_graphics_pipeline_desc &desc, uint64_t stateGuid = 0)
    -> pipeline_cache_handle;
auto API Resolve(pipeline_cache_handle h) -> rhi_graphics_pipeline;

} // namespace PipelineCache

} // namespace nyla
