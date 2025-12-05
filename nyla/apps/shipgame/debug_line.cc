#include "nyla/apps/shipgame/debug_line.h"

namespace nyla
{

namespace
{
using Vertex = Vertex;
}

auto TriangulateLine(const Vec2f &a, const Vec2f &b, float thickness) -> std::vector<Vertex>
{
    const Vec3f green = {0.f, 1.f, 0.f};
    const Vec3f red = {1.f, 0.f, 0.f};
    std::vector<Vertex> out;
    out.reserve(6);

    const float eps = 1e-6f;
    const float half = 0.5f * thickness;

    Vec2f d = Vec2fDif(b, a);
    float l = Vec2fLen(d);

    // Degenerate: draw a tiny square "dot" so something is visible.
    if (l < eps)
    {
        const Vec2f nx{half, 0.0f};
        const Vec2f ny{0.0f, half};

        const Vec2f p0 = Vec2fSum(Vec2fSum(a, nx), ny); // top-right
        const Vec2f p1 = Vec2fSum(Vec2fDif(a, nx), ny); // top-left
        const Vec2f p2 = Vec2fDif(Vec2fDif(a, nx), ny); // bottom-left
        const Vec2f p3 = Vec2fSum(Vec2fDif(a, ny), nx); // bottom-right

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
    const Vec2f t = Vec2fNorm(d);
    const Vec2f nPerp = Vec2fMul(Vec2f{-t[1], t[0]}, half); // (-y, x)

    // Quad corners around the segment
    const Vec2f p0 = Vec2fSum(a, nPerp); // "top" at A
    const Vec2f p1 = Vec2fSum(b, nPerp); // "top" at B
    const Vec2f p2 = Vec2fDif(b, nPerp); // "bottom" at B
    const Vec2f p3 = Vec2fDif(a, nPerp); // "bottom" at A

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