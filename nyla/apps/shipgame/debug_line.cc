#include "nyla/apps/shipgame/debug_line.h"

namespace nyla
{

namespace
{
using Vertex = Vertex;
}

auto TriangulateLine(const float2 &a, const float2 &b, float thickness) -> std::vector<Vertex>
{
    const float3 green = {0.f, 1.f, 0.f};
    const float3 red = {1.f, 0.f, 0.f};
    std::vector<Vertex> out;
    out.reserve(6);

    const float eps = 1e-6f;
    const float half = 0.5f * thickness;

    float2 d = b - a;
    float l = d.Len();

    // Degenerate: draw a tiny square "dot" so something is visible.
    if (l < eps)
    {
        const float2 nx{half, 0.0f};
        const float2 ny{0.0f, half};

        const float2 p0 = ((a + nx) + ny); // top-right
        const float2 p1 = ((a - nx) + ny); // top-left
        const float2 p2 = ((a - nx) - ny); // bottom-left
        const float2 p3 = ((a - ny) + nx); // bottom-right

        // CCW triangles
        out.emplace_back(Vertex{p0, red});
        out.emplace_back(Vertex{p2, red});
        out.emplace_back(Vertex{p1, red});
        out.emplace_back(Vertex{p0, green});
        out.emplace_back(Vertex{p3, green});
        out.emplace_back(Vertex{p2, green});
        return out;
    }

    // Unit direction and left-hand (CCW) perpendicular
    const float2 t = d.Normalized();
    const float2 nPerp = float2{-t[1], t[0]} * half; // (-y, x)

    // Quad corners around the segment
    const float2 p0 = a + nPerp; // "top" at A
    const float2 p1 = b + nPerp; // "top" at B
    const float2 p2 = b - nPerp; // "bottom" at B
    const float2 p3 = a - nPerp; // "bottom" at A

    // Emit triangles with COUNTER-CLOCKWISE winding for Vulkan (default
    // positive-height viewport)
    out.emplace_back(Vertex{p0, red});
    out.emplace_back(Vertex{p2, red});
    out.emplace_back(Vertex{p1, red});

    out.emplace_back(Vertex{p0, green});
    out.emplace_back(Vertex{p3, green});
    out.emplace_back(Vertex{p2, green});

    return out;
}

} // namespace nyla