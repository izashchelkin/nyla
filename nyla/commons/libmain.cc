#include "nyla/commons/libmain.h"

#include <cstdint>

#include "nyla/commons/mem.h"
#include "nyla/commons/mempage_pool.h"
#include "nyla/commons/region_alloc_def.h"

namespace nyla
{

void PlatformBootstrap();
void PlatformTearDown();

void LibMain(void (*userMain)())
{
    PlatformBootstrap();

    uint8_t *addressSpaceBase = (uint8_t *)ReserveMemPages(MemPagePool::kPoolSize);
    CommitMemPages(addressSpaceBase, kPageSize);

    region_alloc bootstrapAlloc{
        .at = addressSpaceBase,
        .begin = addressSpaceBase,
        .end = addressSpaceBase + kPageSize,
        .commitedEnd = addressSpaceBase + kPageSize,
    };
    MemPagePool::Bootstrap(bootstrapAlloc);

    userMain();
    PlatformTearDown();
}

} // namespace nyla