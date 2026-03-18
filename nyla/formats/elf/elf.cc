#include "nyla/formats/elf/elf.h"
#include "nyla/alloc/region_alloc.h"
#include "nyla/commons/assert.h"
#include "nyla/platform/platform.h"

namespace nyla
{

void ElfParser::Parse()
{
    auto elfHeader = Pop<Elf64Header>();

    NYLA_DEBUGBREAK();
}

} // namespace nyla