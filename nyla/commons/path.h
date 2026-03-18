#pragma once

#include "nyla/alloc/region_alloc.h"
#include "nyla/commons/assert.h"
#include <cstring>
#include <string_view>

namespace nyla
{

class Path
{
  public:
    [[nodiscard]]
    static constexpr auto IsSeparator(char ch) -> bool
    {
        return ch == '/' || ch == '\\';
    }

    auto Init(RegionAlloc *alloc) -> Path &
    {
        m_Alloc = alloc;
        m_Base = alloc->GetAt();
        return *this;
    }

    auto EndsWith(std::string_view path) -> bool
    {
        return StrView().ends_with(path);
    }

    auto Clone(RegionAlloc &alloc) -> Path
    {
        return alloc.PushPath().Append(StrView());
    }

    auto Append(std::string_view path) -> Path &
    {
        if (!path.empty())
        {
            if (m_Alloc->GetBytesUsed() > 0)
            {
                m_Alloc->Pop(m_Alloc->GetAt() - 1);

                if (!IsSeparator(*(m_Alloc->GetAt() - 1)))
                    m_Alloc->Push('/');
            }

            m_Alloc->PushCopyStrView(path);
            m_Alloc->Push('\0');
        }

        return *this;
    }

    auto PopBack() -> Path &
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

    auto SetExtension(std::string_view ext) -> Path &
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

        if (!ext.empty())
        {
            if (ext[0] != '.')
                m_Alloc->Push('.');
            m_Alloc->PushCopyStrView(ext);
        }

        m_Alloc->Push('\0');

        return *this;
    }

    [[nodiscard]]
    auto CStr() const -> const char *
    {
        if (m_Base == m_Alloc->GetAt())
            return "";
        return m_Base;
    }

    [[nodiscard]]
    auto StrView() const -> std::string_view
    {
        if (m_Base == m_Alloc->GetAt())
            return {};
        return std::string_view{m_Base, static_cast<size_t>(m_Alloc->GetBytesUsed() - 1)};
    }

  private:
    char *m_Base;
    RegionAlloc *m_Alloc;
};

inline auto RegionAlloc::PushPath() -> Path
{
    Path path;
    path.Init(this);
    return path;
}

inline auto RegionAlloc::PushPath(std::string_view path) -> Path
{
    return PushPath().Append(path);
}

} // namespace nyla