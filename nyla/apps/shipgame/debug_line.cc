#include "nyla/apps/shipgame/debug_line.h"

namespace nyla
{

namespace
{
using Vertex = Vertex;
}

auto TriangulateLine(const Vec2f &A, const Vec2f &B, float thickness) -> std::vector<Vertex>
{
    const Vec3f green = {0.f, 1.f, 0.f};
    const Vec3f red = {1.f, 0.f, 0.f};
    std::vector<Vertex> out;
    out.reserve(6);

    const float eps = 1e-6f;
    const float half = 0.5f * thickness;

    Vec2f d = Vec2fDif(B, A);
    float L = Vec2fLen(d);

    // Degenerate: draw a tiny square "dot" so something is visible.
    if (L < eps)
    {
        const Vec2f nx{half, 0.0f};
        const Vec2f ny{0.0f, half};

        const Vec2f p0 = Vec2fSum(Vec2fSum(A, nx), ny); // top-right
        const Vec2f p1 = Vec2fSum(Vec2fDif(A, nx), ny); // top-left
        const Vec2f p2 = Vec2fDif(Vec2fDif(A, nx), ny); // bottom-left
        const Vec2f p3 = Vec2fSum(Vec2fDif(A, ny), nx); // bottom-right

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
    const Vec2f n_perp = Vec2fMul(Vec2f{-t[1], t[0]}, half); // (-y, x)

    // Quad corners around the segment
    const Vec2f p0 = Vec2fSum(A, n_perp); // "top" at A
    const Vec2f p1 = Vec2fSum(B, n_perp); // "top" at B
    const Vec2f p2 = Vec2fDif(B, n_perp); // "bottom" at B
    const Vec2f p3 = Vec2fDif(A, n_perp); // "bottom" at A

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