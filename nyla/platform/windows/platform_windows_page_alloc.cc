#include "nyla/commons/align.h"
#include "nyla/commons/assert.h"
#include "nyla/platform/windows/platform_windows.h"
#include <cstdint>

namespace nyla
{

auto Platform::Impl::GetMemPageSize() -> uint64_t
{
    return m_SysInfo.dwPageSize;
}

auto Platform::Impl::ReserveMemPages(uint64_t size) -> char *
{
    char *ret = m_AddressSpaceAt;
    m_AddressSpaceAt += AlignedUp<uint64_t>(size, m_SysInfo.dwPageSize);
    return ret;
}

void Platform::Impl::CommitMemPages(char *page, uint64_t size)
{
    NYLA_ASSERT(((page - m_AddressSpaceBase) % m_SysInfo.dwPageSize) == 0);

    AlignUp<uint64_t>(size, m_SysInfo.dwPageSize);

    VirtualAlloc(page, size, MEM_COMMIT, PAGE_READWRITE);
}

void Platform::Impl::DecommitMemPages(char *page, uint64_t size)
{
    NYLA_ASSERT(((page - m_AddressSpaceBase) % m_SysInfo.dwPageSize) == 0);

    AlignUp<uint64_t>(size, m_SysInfo.dwPageSize);

    VirtualAlloc(page, size, MEM_DECOMMIT, PAGE_NOACCESS);
}

//

auto Platform::GetMemPageSize() -> uint64_t
{
    return m_Impl->GetMemPageSize();
}

auto Platform::ReserveMemPages(uint64_t size) -> char *
{
    return m_Impl->ReserveMemPages(size);
}

void Platform::CommitMemPages(char *page, uint64_t size)
{
    return m_Impl->CommitMemPages(page, size);
}

void Platform::DecommitMemPages(char *page, uint64_t size)
{
    return m_Impl->DecommitMemPages(page, size);
}

} // namespace nyla