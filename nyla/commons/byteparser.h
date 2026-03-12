#include "nyla/commons/assert.h"
#include <cstdint>

namespace nyla
{

class ByteParser
{
  public:
    constexpr auto Peek() -> char
    {
        return *m_At;
    }

    constexpr void Advance()
    {
        ++m_At;
        --m_Left;
    }

    constexpr auto Pop() -> char
    {
        const char ret = Peek();
        Advance();
        return ret;
    }

    constexpr auto PopDWord() -> uint32_t
    {
        const auto ret = *(uint32_t *)m_At;
        m_At += sizeof(ret);
        return ret;
    }

    constexpr void SkipWhitespace()
    {
        while (IsWhitespace(Peek()))
            Advance();
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

    //

    constexpr static auto ParseHexByte(const char *ch) -> uint8_t
    {
        uint8_t ret = 0;
        ret |= ParseHexChar(*ch) << 4;
        ret |= ParseHexChar(*(ch + 1));
        return ret;
    }

    constexpr static auto ParseHexChar(char ch) -> uint8_t
    {
        switch (ch)
        {
        case '0':
            return 0;

        case '1':
            return 1;

        case '2':
            return 2;

        case '3':
            return 3;

        case '4':
            return 4;

        case '5':
            return 5;

        case '6':
            return 6;

        case '7':
            return 7;

        case '8':
            return 8;

        case '9':
            return 9;

        case 'a':
        case 'A':
            return 10;

        case 'b':
        case 'B':
            return 11;

        case 'c':
        case 'C':
            return 12;

        case 'd':
        case 'D':
            return 13;

        case 'e':
        case 'E':
            return 14;

        case 'f':
        case 'F':
            return 15;

        default: {
            NYLA_ASSERT(false);
            return 0xFF;
        }
        }
    }

  protected:
    const char *m_At;
    uint32_t m_Left;
};

} // namespace nyla