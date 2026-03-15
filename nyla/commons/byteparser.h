#pragma once

#include "nyla/commons/assert.h"
#include <cstdint>
#include <string_view>

namespace nyla
{

class ByteParser
{
  public:
    void Init(const char *base, uint64_t size)
    {
        m_At = base;
        m_Left = size;
    }

    [[nodiscard]]
    constexpr auto Peek() const -> const char &
    {
        return *m_At;
    }

    constexpr void Advance()
    {
        Advance(1);
    }

    constexpr void Advance(uint64_t i)
    {
        m_At += i;
        m_Left -= i;
        NYLA_ASSERT(m_Left >= 0);
    }

    constexpr auto Pop() -> char
    {
        const char ret = Peek();
        Advance();
        return ret;
    }

    template <typename T> constexpr auto Pop() -> T
    {
        const T ret = *reinterpret_cast<const T *>(m_At);
        m_At += sizeof(ret);
        return ret;
    }

    constexpr auto PopDWord() -> uint32_t
    {
        const auto ret = *(uint32_t *)m_At;
        m_At += sizeof(ret);
        return ret;
    }

    constexpr void SkipUntil(char ch)
    {
        while (Peek() != ch)
            Advance();
    }

    constexpr void NextLine()
    {
        while (Pop() != '\n')
            ;
    }

    constexpr void SkipWhitespace()
    {
        while (IsWhitespace(Peek()))
            Advance();
    }

    constexpr auto StartsWith(std::string_view str) -> bool
    {
        return std::string_view{m_At, m_Left}.starts_with(str);
    }

    constexpr auto StartsWithAdvance(std::string_view str) -> bool
    {
        if (StartsWith(str))
        {
            Advance(str.size());
            return true;
        }
        else
        {
            return false;
        }
    }

    constexpr static auto IsNumber(unsigned char ch) -> bool
    {
        return ch >= '0' && ch <= '9';
    }

    constexpr static auto IsAlpha(unsigned char ch) -> bool
    {
        return (ch >= 'a' && ch < 'z') || (ch >= 'A' && ch < 'Z');
    }

    constexpr static auto IsWhitespace(unsigned char ch) -> bool
    {
        return ch == ' ' || ch == '\n' || ch == '\t';
    }

    //
    //
    constexpr auto ParseLong() -> int64_t
    {
        double d;
        int64_t l;
        NYLA_ASSERT(ParseDecimal(d, l) == ParseNumberResult::Long);
        return l;
    }

    enum ParseNumberResult
    {
        Double,
        Long,
    };

    constexpr auto ParseDecimal(double &outDouble, int64_t &outLong) -> ParseNumberResult
    {
        uint32_t sign = 1;
        if (Peek() == '-')
        {
            sign = -1;
            Advance();
        }

        uint64_t integer = 0;
        uint64_t fraction = 0;
        uint64_t fractionCount = 0;

        while (m_Left > 0 && IsNumber(Peek()))
        {
            integer *= 10;
            integer += Pop() - '0';
        }

        if (Peek() == '.')
        {
            Advance();
            while (m_Left > 0 && IsNumber(Peek()))
            {
                ++fractionCount;
                fraction *= 10;
                fraction += Pop() - '0';
            }

            auto f = static_cast<double>(fraction);
            for (uint32_t i = 0; i < fractionCount; ++i)
                f /= 10.0;

            f += static_cast<double>(integer);

            outDouble = static_cast<double>(sign * f);
            return ParseNumberResult::Double;
        }
        else
        {
            outLong = static_cast<int64_t>(sign * integer);
            return ParseNumberResult::Long;
        }
    }

  protected:
    const char *m_At;
    uint64_t m_Left;
};

} // namespace nyla