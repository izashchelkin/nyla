#include <cstddef>
#include <cstdint>

#include "nyla/apps/terminal/terminal.h"
#include "nyla/commons/bdf/bdf.h"
#include "nyla/commons/engine.h"
#include "nyla/commons/glyph_renderer.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/region_alloc.h"

namespace nyla
{

auto PlatformMain(Span<const char *> argv) -> int
{
    ColorTheme::Init();

    Platform::Init(PlatformInitDesc{
        .enabledFeatures = PlatformFeature::KeyboardInput,
        .open = true,
    });

    RegionAlloc rootAlloc;
    rootAlloc.Init(nullptr, 64_GiB, true);

    Engine::Init({.maxFps = 144, .vsync = true, .rootAlloc = &rootAlloc});

    {
        std::vector<std::byte> data = Platform::ReadFile(Str{R"(D:\nyla\assets\fonts\ter-u32n.bdf)"});
        BdfParser bdfParser;
        bdfParser.Init((char *)data.Data(), data.Size());

        Span<uint8_t> fontAtlas = BuildFontAtlas(bdfParser, rootAlloc);
        GlyphRenderer::Init(fontAtlas.Data(), 1024, 1024);
    }

    return 0;
}

} // namespace nyla