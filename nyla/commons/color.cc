#include "nyla/commons/color.h"
#include "nyla/commons/assert.h"

#include "nyla/commons/math/vec.h"

namespace nyla
{

auto ConvertHsvToRgb(float3 hsv) -> float3
{
    const float h = hsv[0];
    const float s = hsv[1];
    const float v = hsv[2];

    float i = std::floor(h * 6.0f);
    float f = h * 6.0f - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);

    float r, g, b;
    switch (static_cast<int>(i) % 6)
    {
    case 0:
        r = v;
        g = t;
        b = p;
        break;
    case 1:
        r = q;
        g = v;
        b = p;
        break;
    case 2:
        r = p;
        g = v;
        b = t;
        break;
    case 3:
        r = p;
        g = q;
        b = v;
        break;
    case 4:
        r = t;
        g = p;
        b = v;
        break;
    case 5:
        r = v;
        g = p;
        b = q;
        break;
    default:
        NYLA_ASSERT(false);
    }

    return {r, g, b};
}

} // namespace nyla