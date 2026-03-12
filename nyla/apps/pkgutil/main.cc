#include "nyla/formats/elf/elf_parser.h"
#include "nyla/platform/platform.h"

#include <elf.h>

namespace nyla
{

auto PlatformMain() -> int
{
    g_Platform.InitGraphical({});

    return 0;
}

} // namespace nyla