#include "nyla/commons/libmain.h"

#include <cstdint>

#include "nyla/commons/mem.h"
#include "nyla/commons/mempage_pool.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"

namespace nyla
{

void PlatformInit0(); // nothing is inited yet including memory allocators
void PlatformInit1(); // memory alloactors are available
void PlatformTearDown();

void API LibMain(void (*userMain)())
{
    PlatformInit0();

    uint8_t *addressSpaceBase = (uint8_t *)ReserveMemPages(MemPagePool::kPoolSize);
    RegionAlloc::g_BootstrapAlloc = {};
    RegionAlloc::g_BootstrapAlloc.at = addressSpaceBase;
    RegionAlloc::g_BootstrapAlloc.begin = addressSpaceBase;
    RegionAlloc::g_BootstrapAlloc.end = addressSpaceBase + MemPagePool::kChunkSize;

    MemPagePool::Bootstrap();

    PlatformInit1();
    userMain();
    PlatformTearDown();
}

} // namespace nyla