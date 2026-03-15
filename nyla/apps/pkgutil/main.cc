#include "nyla/formats/elf/elf.h"
#include "nyla/platform/platform.h"

namespace nyla
{

auto PlatformMain(std::span<const char *> argv) -> int
{
    Platform::Init({});

    std::vector<std::byte> data = Platform::ReadFile((std::string_view) R"(\\wsl.localhost\archlinux\usr\bin\pacman)");

    RegionAlloc rootAlloc;
    rootAlloc.Init(nullptr, 64_GiB, true);

    RegionAlloc parserAlloc = rootAlloc.PushSubAlloc(1_MiB);
    ElfParser parser;
    parser.Init(&parserAlloc, (const char *)data.data(), data.size());

    parser.Parse();

    return 0;
}

} // namespace nyla