#include "nyla/commons/path.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/str.h"

namespace nyla
{

auto Path::Append(Str path) -> Path &
{
    if (!path.Empty())
    {
        if (m_Alloc->GetBytesUsed() > 0)
        {
            m_Alloc->Pop(m_Alloc->GetAt() - 1);

            if (!IsSeparator(*(m_Alloc->GetAt() - 1)))
                m_Alloc->Push('/');
        }

        m_Alloc->PushCopyStr(path);
        m_Alloc->Push('\0');
    }

    return *this;
}

auto Path::PopBack() -> Path &
{
    if (m_Alloc->GetBytesUsed() > 0)
    {
        m_Alloc->Pop(m_Alloc->GetAt() - 1);

        if (m_Alloc->GetBytesUsed() > 0 && IsSeparator(*(m_Alloc->GetAt() - 1)))
        {
            m_Alloc->Pop(m_Alloc->GetAt() - 1);
        }

        while (m_Alloc->GetBytesUsed() > 0)
        {
            if (IsSeparator(*(m_Alloc->GetAt() - 1)))
                break;
            else
                m_Alloc->Pop(m_Alloc->GetAt() - 1);
        }

        m_Alloc->Push('\0');
    }

    return *this;
}

auto Path::SetExtension(Str ext) -> Path &
{
    if (m_Alloc->GetBytesUsed() == 0)
        NYLA_ASSERT(false);

    char *currentEnd = m_Alloc->GetAt() - 1;
    m_Alloc->Pop(currentEnd);

    char *cursor = currentEnd;
    char *start = m_Base;

    char *rewindTarget = currentEnd;

    while (cursor > start)
    {
        char c = *(cursor - 1);

        if (IsSeparator(c))
            break;

        if (c == '.')
        {
            rewindTarget = cursor - 1;
            break;
        }

        --cursor;
    }

    if (rewindTarget < currentEnd)
        m_Alloc->Pop(rewindTarget);

    if (!ext.Empty())
    {
        if (ext[0] != '.')
            m_Alloc->Push('.');
        m_Alloc->PushCopyStr(ext);
    }

    m_Alloc->Push('\0');

    return *this;
}

[[nodiscard]]
auto Path::CStr() const -> const char *
{
    if (m_Base == m_Alloc->GetAt())
        return "";
    return m_Base;
}

[[nodiscard]]
auto Path::GetStr() const -> Str
{
    if (m_Base == m_Alloc->GetAt())
        return {};
    return Str{m_Base, m_Alloc->GetBytesUsed() - 1};
}

auto Path::Init(RegionAlloc *alloc) -> Path &
{
    m_Alloc = alloc;
    m_Base = alloc->GetAt();
    return *this;
}

auto Path::EndsWith(Str path) const -> bool
{
    return Str().EndsWith(path);
}

} // namespace nyla