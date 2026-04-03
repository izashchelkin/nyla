#include <cstdint>

#include "nyla/commons/region_alloc.h"
#include "nyla/commons/bdf/bdf.h"

namespace nyla
{

auto BuildFontAtlas(BdfParser &parser, RegionAlloc &alloc) -> Span<uint8_t>
{
    auto textureAtlas = alloc.Push<Array<Array<uint8_t, 1024>, 1024>>();

    uint32_t iglyph = 0;
    auto scratch = alloc.PushSubAlloc(1_KiB);
    for (BdfGlyph glyph; parser.NextGlyph(&scratch, glyph); scratch.Reset())
    {
        const uint32_t glyphX = iglyph % 64;
        const uint32_t glyphY = iglyph / 64;
        ++iglyph;

        uint32_t ipixel = 0;
        for (uint32_t b : glyph.bitmap)
        {
            for (uint32_t i = 0; i < 8; ++i)
            {
                const uint32_t pixelX = ipixel % 16;
                const uint32_t pixelY = ipixel / 16;
                ++ipixel;

                const uint32_t x = glyphX * 16 + pixelX;
                const uint32_t y = glyphY * 32 + pixelY;

                (*textureAtlas)[y][x] = ((b >> (7 - i)) & 1) * std::numeric_limits<uint8_t>::max();
            }
        }

        NYLA_ASSERT(ipixel == 16 * 32);
    }
    alloc.Pop(scratch.GetBase());

    auto *data = (uint8_t *)textureAtlas->data();
    constexpr size_t kSize = sizeof(*textureAtlas) / sizeof(uint8_t);
    return Span{data, kSize};
}

} // namespace nyla