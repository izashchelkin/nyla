#pragma once

#include <cstdint>

#include "nyla/commons/fmt.h"
#include "nyla/commons/span.h"

namespace nyla
{

INLINE uint16_t ByteSwap16(uint16_t val)
{
#if defined(_MSC_VER)
    return _byteswap_ushort(val);
#else
    return __builtin_bswap16(val);
#endif
}

INLINE uint32_t ByteSwap32(uint32_t val)
{
#if defined(_MSC_VER)
    return _byteswap_ulong(val);
#else
    return __builtin_bswap32(val);
#endif
}

INLINE uint64_t ByteSwap64(uint64_t val)
{
#if defined(_MSC_VER)
    return _byteswap_uint64(val);
#else
    return __builtin_bswap64(val);
#endif
}

inline auto IsNumber(uint8_t ch) -> bool
{
    return ch >= '0' && ch <= '9';
}

inline auto IsAlpha(uint8_t ch) -> bool
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch < 'Z');
}

inline auto IsWhitespace(uint8_t ch) -> bool
{
    return ch == ' ' || ch == '\n' || ch == '\t';
}

struct ByteParser
{
    const uint8_t *m_At;
    uint64_t m_Left;

    void Init(const uint8_t *base, uint64_t size)
    {
        m_At = base;
        m_Left = size;
    }

    auto Left() -> uint64_t
    {
        return m_Left;
    }

    [[nodiscard]]
    auto Peek() const -> const uint8_t &
    {
        return *m_At;
    }

    void Advance()
    {
        Advance(1);
    }

    void Advance(uint64_t i)
    {
        m_At += i;
        m_Left -= i;
        NYLA_DASSERT(m_Left >= 0);
    }

    auto Pop() -> uint8_t
    {
        const uint8_t ret = Peek();
        Advance();
        return ret;
    }

    template <typename T> auto Pop() -> T
    {
        const T ret = LoadU<T>(m_At);
        Advance(sizeof(ret));
        return ret;
    }

    auto Pop16() -> uint16_t
    {
        return Pop<uint16_t>();
    }

    auto Pop16BE() -> uint16_t
    {
        return ByteSwap16(Pop16());
    }

    auto Pop32() -> uint32_t
    {
        return Pop<uint32_t>();
    }

    auto Pop32BE() -> uint32_t
    {
        return ByteSwap32(Pop32());
    }

    auto Pop64() -> uint64_t
    {
        return Pop<uint64_t>();
    }

    auto Pop64BE() -> uint64_t
    {
        return ByteSwap64(Pop64());
    }

    //

    void SkipUntil(uint8_t ch)
    {
        while (Peek() != ch)
            Advance();
    }

    void NextLine()
    {
        while (Pop() != '\n')
            ;
    }

    void SkipWhitespace()
    {
        while (IsWhitespace(Peek()))
            Advance();
    }

    auto StartsWith(Str str) -> bool
    {
        return AsStr((const char *)m_At, m_Left).StartsWith(str);
    }

    auto StartsWithAdvance(Str str) -> bool
    {
        if (StartsWith(str))
        {
            Advance(str.Size());
            return true;
        }
        else
        {
            return false;
        }
    }

    auto ParseLong() -> int64_t
    {
        double d;
        int64_t l;
        const ParseNumberResult res = ParseDecimal(d, l);
        NYLA_ASSERT(res == ParseNumberResult::Long);
        return l;
    }

    enum ParseNumberResult
    {
        Double,
        Long,
    };

    auto ParseDecimal(double &outDouble, int64_t &outLong) -> ParseNumberResult
    {
        int32_t sign = 1;
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
};

} // namespace nyla