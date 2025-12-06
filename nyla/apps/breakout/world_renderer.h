#pragma once

#include "nyla/commons/math/vec.h"
#include "nyla/fwk/render_pipeline.h"

namespace nyla
{

struct Vertex
{
    float2 pos;
    float2 pad0{666.f, 666.f};

    explicit Vertex(float2 pos) : pos{pos}
    {
    }
};

extern Rp worldPipeline;

void WorldSetUp();
void WorldRender(float2 pos, float3 color, float scaleX, float scaleY, const RpMesh &mesh);

} // namespace nyla