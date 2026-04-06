#pragma once

#include <cstdint>

#include "nyla/commons/array.h"
#include "nyla/commons/fmt.h"

namespace nyla
{

template <uint64_t N> struct InlineString
{
    Array<char, N + 1> m_Data;
    uint64_t m_Size;

    void AppendChar(char ch)
    {
        NYLA_DASSERT(m_Size < N);
        m_Data[m_Size++] = ch;
        m_Data[m_Size] = '\0';
    }

    [[nodiscard]]
    auto Size() const -> uint64_t
    {
        return m_Size;
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
    NYLA_ASSERT(str.Size() <= N + 1);

    InlineString<N> ret{
        .m_Data = str.Data(),
        .m_Size = str.Size(),
    };
    return ret;
}

template <uint64_t N> auto StringWriteFmt(InlineString<N> out, Str fmt, ...) -> Str
{
    va_list args;
    va_start(args, fmt);

    StringWriteFmt(out.Data(), out.Size(), fmt, args);

    va_end(args);

    return out.GetStr();
}

} // namespace nyla