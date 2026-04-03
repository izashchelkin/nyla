#pragma once

#include <cstdint>

#include "nyla/commons/array.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/platform.h"

namespace nyla
{

template <uint64_t N> struct InlineString
{
    uint64_t m_Size;
    Array<char, N + 1> m_Data;

    void AppendChar(char ch)
    {
        NYLA_DASSERT(m_Size < N);
        m_Data[m_Size++] = ch;
        m_Data[m_Size] = '\0';
    }

    void AppendChar(unsigned char ch)
    {
        AppendChar(static_cast<char>(ch));
    }

    void AppendChar(uint32_t ch)
    {
        AppendChar(static_cast<char>(ch));
    }

    [[nodiscard]]
    auto CStr() const -> const char *
    {
        return m_Data.Data();
    }

    [[nodiscard]]
    auto Data() -> char *
    {
        return m_Data.Data();
    }

    [[nodiscard]]
    auto GetStr() const -> Str
    {
        return Str{m_Data.Data(), m_Size};
    }

    auto operator==(Str rhs) const -> bool
    {
        return this->GetStr() == rhs;
    }

    void AsciiToUpper()
    {
        for (uint32_t i = 0; i < m_Size; ++i)
        {
            char &ch = m_Data[i];
            if (ch >= 'a' && ch <= 'z')
                ch = ch - ('a' - 'A');
            else
                ch = ch;
        }
    }
};

template <uint64_t N> auto AsInlineStr(Str str) -> InlineString<N>
{
    NYLA_ASSERT(str.Size() <= N);

    InlineString<N> ret{
        .m_Data = str.Data(),
        .m_Size = str.Size(),
    };
    return ret;
}

} // namespace nyla