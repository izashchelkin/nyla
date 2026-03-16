#include "nyla/alloc/region_alloc.h"
#include "nyla/commons/assert.h" #include "nyla/commons/log.h"
#include "nyla/formats/bdf/bdf.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"
#include <cstdint>
#include <cstdio>

namespace nyla
{

auto PlatformMain(std::span<const char *> argv) -> int
{
    Platform::Init(PlatformInitDesc{
        .enabledFeatures = PlatformFeature::KeyboardInput,
        .open = true,
    });

    g_Rhi.Init(RhiInitDesc{
        .flags = RhiFlags::VSync,
        .limits =
            {
                .numTextures = 0,
                .numTextureViews = 0,
                .numBuffers = 0,
                .numSamplers = 0,

                .numFramesInFlight = 1,
                .maxDrawCount = 1,
                .maxPassCount = 1,

                .frameConstantSize = 0,
                .passConstantSize = 0,
                .drawConstantSize = 0,
                .largeDrawConstantSize = 320,
            },
    });

    RegionAlloc rootAlloc;
    rootAlloc.Init(nullptr, 64_GiB, true);

    RegionAlloc parserAlloc = rootAlloc.PushSubAlloc(1_MiB);
    std::vector<std::byte> data = Platform::ReadFile(std::string_view{R"(D:\nyla\assets\fonts\ter-u32n.bdf)"});
    BdfParser bdfParser;
    bdfParser.Init(&parserAlloc, (char *)data.data(), data.size());

    BdfGlyph glyph;
    while (bdfParser.NextGlyph(glyph))
    {
        uint32_t i = 0;
        for (auto b : glyph.bitmap)
        {
            fprintf(stdout, "%c%c%c%c%c%c%c%c",
                    ((b >> 7) & 1) ? 'X' : ' ', // Left-most pixel
                    ((b >> 6) & 1) ? 'X' : ' ', ((b >> 5) & 1) ? 'X' : ' ', ((b >> 4) & 1) ? 'X' : ' ',
                    ((b >> 3) & 1) ? 'X' : ' ', ((b >> 2) & 1) ? 'X' : ' ', ((b >> 1) & 1) ? 'X' : ' ',
                    ((b >> 0) & 1) ? 'X' : ' ');

            if ((++i) % 2 == 0)
            {
                fprintf(stdout, "\n");
                fflush(stdout);
            }
        }

        fprintf(stdout, "\n\n");
        getc(stdin);
    }
    return 0;
}

} // namespace nyla