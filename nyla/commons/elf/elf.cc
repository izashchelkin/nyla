#include "nyla/commons/elf/elf.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/platform.h"

namespace nyla
{

void ElfParser::Parse()
{
    auto elfHeader = Pop<Elf64Header>();

    NYLA_DEBUGBREAK();
}

} // namespace nyla