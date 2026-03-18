#include <cstddef>
#include <cstdint>

#include "nyla/alloc/region_alloc.h"
#include "nyla/apps/terminal/terminal.h"
#include "nyla/engine/engine.h"
#include "nyla/engine/glyph_renderer.h"
#include "nyla/formats/bdf/bdf.h"
#include "nyla/platform/platform.h"

namespace nyla
{

auto PlatformMain(std::span<const char *> argv) -> int
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
        std::vector<std::byte> data = Platform::ReadFile(std::string_view{R"(D:\nyla\assets\fonts\ter-u32n.bdf)"});
        BdfParser bdfParser;
        bdfParser.Init((char *)data.data(), data.size());

        std::span<uint8_t> fontAtlas = BuildFontAtlas(bdfParser, rootAlloc);
        GlyphRenderer::Init(fontAtlas.data(), 1024, 1024);
    }

    return 0;
}

} // namespace nyla