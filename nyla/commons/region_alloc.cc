#include "nyla/commons/region_alloc.h"

#include <cstdint>

#include "nyla/commons/align.h"
#include "nyla/commons/log.h"
#include "nyla/commons/minmax.h"
#include "nyla/commons/platform.h"

namespace nyla
{

void RegionAlloc::Pop(void *p)
{
    m_Used = (char *)p - m_Base;
    NYLA_ASSERT(m_Used <= m_Size);
}

void RegionAlloc::Init(void *base, uint64_t maxSize, bool ownsPages)
{
    NYLA_ASSERT(ownsPages || base);

    m_OwnsPages = ownsPages;
    m_Used = 0;
    m_Size = 0;
    m_MaxSize = maxSize;

    if (base)
        m_Base = (char *)base;
    else
        m_Base = Platform::ReserveMemPages(maxSize);
}

auto RegionAlloc::PushBytes(uint64_t requestedSize, uint64_t align) -> char *
{
    AlignUp<uint64_t>(m_Used, Max(kMinAlign, align));
    char *const ret = m_Base + m_Used;
    m_Used += requestedSize;

    if (m_Used > m_Size)
    {
        NYLA_ASSERT(m_Used <= m_MaxSize);

        if (m_OwnsPages)
        {
            AlignUp(m_Used, Platform::GetMemPageSize());
            NYLA_ASSERT(m_Used <= m_MaxSize);

            char *const p = m_Base + m_Size;
            Platform::CommitMemPages(p, m_Used - m_Size);
        }

        this->m_Size = m_Used;
    }

    return ret;
}

auto RegionAlloc::PushSubAlloc(uint64_t requestedSize) -> RegionAlloc
{
    void *const p = PushBytes(requestedSize, Platform::GetMemPageSize());
    RegionAlloc subAlloc{};
    subAlloc.Init(p, requestedSize, false);
    return subAlloc;
}

} // namespace nyla