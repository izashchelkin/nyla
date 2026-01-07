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
        memcpy(m_Data, str.data(), m_Size);
        m_Data[m_Size + 1] = '\0';
    }

    void AppendChar(char ch)
    {
        NYLA_ASSERT(m_Size < N);
        m_Data[m_Size++] = ch;
        m_Data[m_Size] = '\0';
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

  private:
    uint32_t m_Size;
    std::array<char, N + 1> m_Data;
};