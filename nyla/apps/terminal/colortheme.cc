#include <cmath>
#include <cstdint>

#include "nyla/apps/terminal/terminal.h"
#include "nyla/commons/lerp.h"
#include "nyla/commons/vec.h"

namespace nyla
{

namespace
{

constexpr auto PackRGB(uint8_t r, uint8_t g, uint8_t b) -> uint32_t
{
    return (r << 16) | (g << 8) | b;
}

} // namespace

uint32_t ColorTheme::bg = PackRGB(28, 28, 28);
uint32_t ColorTheme::fg = PackRGB(188, 188, 188);

Array<uint32_t, 256> ColorTheme::palette{
    PackRGB(28, 28, 28),    // 0: Black
    PackRGB(215, 95, 95),   // 1: Red
    PackRGB(135, 175, 135), // 2: Green
    PackRGB(175, 175, 135), // 3: Yellow
    PackRGB(95, 135, 175),  // 4: Blue
    PackRGB(175, 135, 175), // 5: Magenta
    PackRGB(95, 135, 135),  // 6: Cyan
    PackRGB(188, 188, 188), // 7: White
    PackRGB(118, 118, 118), // 8: Bright Black
    PackRGB(215, 135, 135), // 9: Bright Red
    PackRGB(175, 215, 175), // 10: Bright Green
    PackRGB(215, 215, 175), // 11: Bright Yellow
    PackRGB(135, 175, 215), // 12: Bright Blue
    PackRGB(215, 175, 215), // 13: Bright Magenta
    PackRGB(135, 175, 175), // 14: Bright Cyan
    PackRGB(238, 238, 238), // 15: Bright White
};

namespace
{

constexpr auto PackRGB(float3 rgb) -> uint32_t
{
    return PackRGB(static_cast<uint8_t>(rgb[0] * 255.f), static_cast<uint8_t>(rgb[1] * 255.f),
                   static_cast<uint8_t>(rgb[2] * 255.f));
}

constexpr void UnpackRGB(uint32_t color, uint8_t &r, uint8_t &g, uint8_t &b)
{
    r = (color >> 16) & 0xFF;
    g = (color >> 8) & 0xFF;
    b = color & 0xFF;
}

constexpr auto UnpackRGBf(uint32_t color) -> float3
{
    uint8_t r, g, b;
    UnpackRGB(color, r, g, b);

    return {(float)r / 255.f, (float)g / 255.f, (float)b / 255.f};
}

auto RGBToLAB(uint32_t color) -> float3
{
    float3 rgb = UnpackRGBf(color);

    auto pivotRgb = [](float &n) -> void {
        if (n > 0.04045)
            n = std::pow((n + 0.055f) / 1.055f, 2.4f);
        else
            n = n / 12.92f;
    };

    for (uint32_t i = 0; i < 3; ++i)
        pivotRgb(rgb[i]);

    auto pivotXyz = [](float n) -> float {
        if (n > 0.008856f)
            return std::pow(n, (1 / 3.f));
        else
            return (7.787f * n) + (16.f / 116.f);
    };

    float x = pivotXyz(((rgb[0] * 0.4124564f + rgb[1] * 0.3575761f + rgb[2] * 0.1804375f) * 100.0f) / 95.047f);
    float y = pivotXyz(((rgb[0] * 0.2126729f + rgb[1] * 0.7151522f + rgb[2] * 0.0721750f) * 100.0f) / 100.000f);
    float z = pivotXyz(((rgb[0] * 0.0193339f + rgb[1] * 0.1191920f + rgb[2] * 0.9503041f) * 100.0f) / 108.883f);

    float l = Max(0.f, (116.f * y) - 16.f);
    float a = 500.f * (x - y);
    float b = 200.f * (y - z);

    return float3{l, a, b};
}

auto LABToRGB(float3 lab) -> float3
{
    float y = (lab[0] + 16.f) / 116.f;
    float x = lab[1] / 500.f + y;
    float z = y - lab[2] / 200.f;

    auto pivotXyzRev = [](float n) -> float {
        float n3 = std::pow(n, 3.f);
        if (n3 > 0.008856)
            return n3;
        else
            return (n - 16.0f / 116.0f) / 7.787f;
    };

    x = pivotXyzRev(x) * 95.047f / 100.f;
    y = pivotXyzRev(y) * 100.000f / 100.f;
    z = pivotXyzRev(z) * 108.883f / 100.f;

    auto pivotRgbRev = [](float n) -> float {
        n = Max(0.f, std::min(1.f, n));
        if (n > 0.0031308f)
            return 1.055f * std::pow(n, (1.f / 2.4f)) - 0.055f;
        else
            return 12.92f * n;
    };

    float r = pivotRgbRev(x * 3.2404542f + y * -1.5371385f + z * -0.4985314f);
    float g = pivotRgbRev(x * -0.9692660f + y * 1.8760108f + z * 0.0415560f);
    float b = pivotRgbRev(x * 0.0556434f + y * -0.2040259f + z * 1.0572252f);

    return {r, g, b};
}

} // namespace

void ColorTheme::Init()
{
    // Thank you Jake
    // https://gist.github.com/jake-stewart/0a8ea46159a7da2c808e5be2177e1783

    constexpr bool kHarmonious = false;

    Array<float3, 8> base8Lab{
        RGBToLAB(bg),         RGBToLAB(palette[1]), RGBToLAB(palette[2]), RGBToLAB(palette[3]),
        RGBToLAB(palette[4]), RGBToLAB(palette[5]), RGBToLAB(palette[6]), RGBToLAB(fg),
    };

    bool isLightTheme = base8Lab[7][0] < base8Lab[0][0];

    if constexpr (!kHarmonious)
    {
        if (isLightTheme)
            std::swap(base8Lab[0], base8Lab[7]);
    }

    int i = 16;

    for (uint32_t r = 0; r < 6; ++r)
    {
        float3 c0 = Lerp(base8Lab[0], base8Lab[1], static_cast<float>(r) / 5.f);
        float3 c1 = Lerp(base8Lab[2], base8Lab[3], static_cast<float>(r) / 5.f);
        float3 c2 = Lerp(base8Lab[4], base8Lab[5], static_cast<float>(r) / 5.f);
        float3 c3 = Lerp(base8Lab[6], base8Lab[7], static_cast<float>(r) / 5.f);

        for (uint32_t g = 0; g < 6; ++g)
        {
            float3 c4 = Lerp(c0, c1, static_cast<float>(g) / 5.f);
            float3 c5 = Lerp(c2, c3, static_cast<float>(g) / 5.f);

            for (uint32_t b = 0; b < 6; ++b)
            {
                float3 c6 = Lerp(c4, c5, static_cast<float>(b) / 5.f);
                palette[i++] = PackRGB(LABToRGB(c6));
            }
        }
    }

    for (uint32_t j = 0; j < 24; ++j)
    {
        float t = static_cast<float>(i + 1) / 25.f;
        palette[i++] = PackRGB(LABToRGB(Lerp(base8Lab[0], base8Lab[7], t)));
    }
}

} // namespace nyla