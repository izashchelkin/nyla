#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "nyla/alloc/region_alloc.h"
#include "nyla/apps/terminal/terminal.h"
#include "nyla/commons/assert.h"
#include "nyla/formats/bdf/bdf.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"

namespace nyla
{

namespace
{

void SavePGM(const char *filename, uint8_t *data, uint32_t w, uint32_t h)
{
    FILE *f;
    NYLA_ASSERT(fopen_s(&f, filename, "wb") == 0);

    fprintf(f, "P5\n%d %d\n255\n", w, h);
    fwrite(data, 1, static_cast<size_t>(w) * h, f);
    fclose(f);
}

} // namespace

auto PlatformMain(std::span<const char *> argv) -> int
{
    ColorTheme::Init();

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

    std::vector<std::byte> data = Platform::ReadFile(std::string_view{R"(D:\nyla\assets\fonts\ter-u32n.bdf)"});
    BdfParser bdfParser;
    bdfParser.Init((char *)data.data(), data.size());

    auto textureAtlas = BuildFontAtlas(bdfParser, rootAlloc);

    SavePGM(R"(D:\test.pgm)", textureAtlas.data(), 1024, 1024);

    return 0;
}

} // namespace nyla