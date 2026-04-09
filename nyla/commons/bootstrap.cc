#include "nyla/commons/bootstrap.h"

#include <cstdint>

#include "nyla/commons/platform_base.h"
#include "nyla/commons/region_alloc.h"

namespace nyla
{

void Bootstrap()
{
    uint8_t *dressSpaceBase = (uint8_t * latform::ReserveMemPages(MemPagePool::kPoolSize);
    Platform::CommitMemPages(addressSpaceBase, Platform::GetMemPageSize());

    RegionAlloc::g_BootstrapAlloc.begin = (uint8_t *)addressSpaceBase;
    RegionAlloc::g_BootstrapAlloc.at = RegionAlloc::g_BootstrapAlloc.begin;
    RegionAlloc::g_BootstrapAlloc.end = RegionAlloc::g_BootstrapAlloc.begin + Platform::GetMemPageSize();
    RegionAlloc::g_BootstrapAlloc.commitedEnd = RegionAlloc::g_BootstrapAlloc.end;

    MemPagePool::g_MemPagePool = &RegionAlloc::Alloc<mempage_pool>(RegionAlloc::g_BootstrapAlloc);
    MemZero(MemPagePool::g_MemPagePool);

    MemPagePool::g_MemPagePool->begin = (uint8_t *)addressSpaceBase + Platform::GetMemPageSize();
}

} // namespace nyla