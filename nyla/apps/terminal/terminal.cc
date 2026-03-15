#include "nyla/alloc/region_alloc.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/log.h"
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
        printf("\n\n");
        for (uint32_t i = 0; i < 32; ++i)
        {
            for (uint32_t j = 0; j < 16; ++j)
            {
                glyph.bitmap.data();

                printf("%d", *(+(i * 32ULL) * j));
            }
            printf("\n");
        }
    }

    return 0;
}

} // namespace nyla