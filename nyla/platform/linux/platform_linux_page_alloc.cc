#include "nyla/commons/align.h"
#include "nyla/commons/assert.h"
#include "nyla/platform/linux/platform_linux.h"
#include <cstdint>
#include <sys/mman.h>
#include <unistd.h>

namespace nyla
{

auto Platform::Impl::GetMemPageSize() -> uint32_t
{
    return m_PageSize;
}

auto Platform::Impl::ReserveMemPages(uint32_t size) -> char *
{
    char *ret = m_AddressSpaceAt;
    m_AddressSpaceAt += AlignedUp(size, m_PageSize);
    return ret;
}

void Platform::Impl::CommitMemPages(char *page, uint32_t size)
{
    NYLA_ASSERT(((page - m_AddressSpaceBase) % m_PageSize) == 0);

    AlignUp(size, GetMemPageSize());

    mprotect(page, size, PROT_READ | PROT_WRITE);
}

void Platform::Impl::DecommitMemPages(char *page, uint32_t size)
{
    NYLA_ASSERT(((page - m_AddressSpaceBase) % m_PageSize) == 0);

    AlignUp(size, GetMemPageSize());

    mprotect(page, size, PROT_NONE);
    madvise(page, size, MADV_DONTNEED);
}

//

auto Platform::GetMemPageSize() -> uint32_t
{
    return m_Impl->GetMemPageSize();
}

auto Platform::ReserveMemPages(uint32_t size) -> char *
{
    return m_Impl->ReserveMemPages(size);
}

void Platform::CommitMemPages(char *page, uint32_t size)
{
    return m_Impl->CommitMemPages(page, size);
}

void Platform::DecommitMemPages(char *page, uint32_t size)
{
    return m_Impl->DecommitMemPages(page, size);
}

} // namespace nyla