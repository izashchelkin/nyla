#include "nyla/commons/log.h"
#include "nyla/formats/bdf/bdf.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"
#include <cstdint>

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

    std::vector<std::byte> data = Platform::ReadFile(std::string_view{R"(D:\nyla\resources\fonts\ter-u32n.bdf)"});
    BdfParser bdfParser;
    bdfParser.Init((char *)data.data(), data.size());

    uint32_t i = 0;
    BdfGlyph glyph;
    while (bdfParser.NextGlyph(glyph))
    {
    }

    return 0;
}

} // namespace nyla