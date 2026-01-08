#pragma once

#include "nyla/commons/assert.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

template <uint32_t N> class InlineString
{
  public:
    InlineString() : m_Size{0}
    {
    }

    InlineString(const char *str) : InlineString(std::string_view{str, strlen(str)})
    {
    }

    InlineString(std::string_view str) : m_Size{static_cast<uint32_t>(str.size())}
    {
        NYLA_ASSERT(m_Size <= N);
        memcpy(m_Data.data(), str.data(), m_Size);
        m_Data[m_Size + 1] = '\0';
    }

    void AppendChar(char ch)
    {
        NYLA_ASSERT(m_Size < N);
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
    auto StringView() const -> std::string_view
    {
        return std::string_view{m_Data.data(), m_Size};
    }

    template <uint32_t M> auto operator==(const InlineString<M> rhs) const -> bool
    {
        return this->StringView() == rhs.StringView();
    }

    auto operator==(std::string_view rhs) const -> bool
    {
        return this->StringView() == rhs;
    }

    auto operator==(const char *rhs) const -> bool
    {
        return this->StringView() == std::string_view{rhs};
    }

    void AsciiToUpper()
    {
        for (uint32_t i = 0; i < m_Size; ++i)
        {
            char& ch = m_Data[i];
            if (ch >= 'a' && ch <= 'z')
                ch = ch - ('a' - 'A');
            else
                ch = ch;
        }
    }

  private:
    uint32_t m_Size;
    std::array<char, N + 1> m_Data;
};