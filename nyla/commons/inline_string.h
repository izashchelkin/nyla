#pragma once

#include <cstdint>

#include "nyla/commons/array.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/str.h"

namespace nyla
{

template <uint32_t N> class InlineString
{
  public:
    InlineString() : m_Size{0}
    {
    }

    InlineString(Str str) : m_Size{str.Size()}
    {
        NYLA_DASSERT(m_Size <= N);
        MemCpy(m_Data.Data(), str.Data(), m_Size);
        m_Data[m_Size + 1] = '\0';
    }

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
    auto CString() const -> const char *
    {
        return m_Data.data();
    }

    [[nodiscard]]
    auto GetStr() const -> Str
    {
        return Str{m_Data.data(), m_Size};
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

  private:
    uint64_t m_Size;
    Array<char, N + 1> m_Data;
};

} // namespace nyla