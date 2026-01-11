#include "nyla/commons/memory/region_alloc.h"
#include "nyla/commons/align.h"
#include "nyla/commons/assert.h"
#include "nyla/platform/platform.h"
#include <cstdint>

namespace nyla
{

RegionAllocator::RegionAllocator(uint32_t size) : m_Size{size}, m_Commited{g_Platform->GetMemPageSize()}
{
    m_Base = g_Platform->ReserveMemPages(size);
    g_Platform->CommitMemPages(m_Base, m_Commited);
}

auto RegionAllocator::PushBytes(uint32_t size, uint32_t align) -> char *
{
    AlignUp(m_Used, align);
    char *const ret = m_Base + m_Used;
    m_Used += size;
    NYLA_ASSERT(m_Used <= m_Size);

    if (m_Commited < m_Used)
    {
        const uint32_t oldCommited = m_Commited;
        m_Commited = AlignedUp(m_Used, g_Platform->GetMemPageSize());
        g_Platform->CommitMemPages(m_Base + oldCommited, m_Commited - oldCommited);
    }

    return ret;
}

void RegionAllocator::Pop(void *p)
{
    m_Used = (char *)p - m_Base;
    NYLA_ASSERT(m_Used <= m_Size);
}

void RegionAllocator::Reset()
{
    m_Used = 0;
}

auto RegionAllocator::GetBytesUsed() -> uint32_t
{
    return m_Used;
}

} // namespace nyla
