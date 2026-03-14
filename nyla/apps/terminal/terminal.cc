#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"

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

    return 0;
}

} // namespace nyla