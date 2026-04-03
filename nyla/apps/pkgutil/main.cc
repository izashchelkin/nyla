#include "nyla/commons/elf/elf.h"
#include "nyla/commons/platform.h"

namespace nyla
{

auto PlatformMain(Span<const char *> argv) -> int
{
    Platform::Init({});

    std::vector<std::byte> data = Platform::ReadFile((Str) R"(\\wsl.localhost\archlinux\usr\bin\pacman)");

    RegionAlloc rootAlloc;
    rootAlloc.Init(nullptr, 64_GiB, true);

    RegionAlloc parserAlloc = rootAlloc.PushSubAlloc(1_MiB);
    ElfParser parser;
    parser.Init(&parserAlloc, (const char *)data.Data(), data.Size());

    parser.Parse();

    return 0;
}

} // namespace nyla