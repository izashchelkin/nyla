#pragma once

#include "nyla/commons/math/vec.h"
#include "nyla/fwk/render_pipeline.h"

namespace nyla
{

struct Vertex
{
    float2 pos;
    std::array<float, 2> pad0{666.f, 666.f};
    float3 color;
    std::array<float, 1> pad1{777.f};

    Vertex(float2 pos, float3 color) : pos{pos}, color{color}
    {
    }
};

extern Rp worldPipeline;
extern Rp gridPipeline;

void WorldSetUp(float2 gameCameraPos, float gameCameraZoom);
void WorldRender(float2 pos, float angleRadians, float scalar, std::span<Vertex> vertices);
void GridRender();

} // namespace nyla