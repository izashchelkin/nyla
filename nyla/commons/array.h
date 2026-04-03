#pragma once

#include <cstdarg>
#include <cstdint>
#include <type_traits>

#include "nyla/commons/mem.h"
#include "nyla/commons/platform_base.h"

namespace nyla
{

void NYLA_API FileWriteFmt(FileHandle handle, const char *fmt, uint64_t fmtSize, ...);

#define NYLA_LOG(fmt, ...) ::nyla::FileWriteFmt(Platform::GetStderr(), fmt, CStrLen(fmt), ##__VA_ARGS__)

#define NYLA_SV_FMT "%.*s"
#define NYLA_SV_ARG(sv) (uint32_t)((sv).Size()), (sv).Data()

#if defined(_MSC_VER)
#define NYLA_DEBUGBREAK() __debugbreak()
#elif defined(__clang__)
#if __has_builtin(__builtin_debugtrap)
#define NYLA_DEBUGBREAK() __builtin_debugtrap()
#else
#define NYLA_DEBUGBREAK() __builtin_trap()
#endif
#elif defined(__GNUC__)
#define NYLA_DEBUGBREAK() __builtin_trap()
#else
#define NYLA_DEBUGBREAK()                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        *(volatile int *)0 = 0;                                                                                        \
    } while (0)
#endif

#define NYLA_ASSERT(cond)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            NYLA_LOG("%s:%d: assertion failed: %s", __FILE__, __LINE__, #cond);                                        \
            NYLA_DEBUGBREAK();                                                                                         \
            *(volatile int *)(0) = 0;                                                                                  \
        }                                                                                                              \
    } while (0)

#ifdef NDEBUG
#define NYLA_DASSERT(cond)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        (void)sizeof(cond);                                                                                            \
    } while (0)
#else
#define NYLA_DASSERT(cond) NYLA_ASSERT(cond)
#endif

template <typename T> struct Span
{
    static_assert(std::is_trivially_constructible<T>());
    static_assert(std::is_trivially_destructible<T>());

    T *m_Data;
    uint64_t m_Size;

    auto AsConst() -> Span<const T>
        requires(!std::is_const<T>())
    {
        return {Data(), Size()};
    }

    [[nodiscard]]
    auto operator[](uint64_t i) -> T &
    {
        return m_Data[i];
    }

    [[nodiscard]]
    auto operator[](uint64_t i) const -> const T &
    {
        return m_Data[i];
    }

    [[nodiscard]]
    auto Empty() const -> bool
    {
        return m_Size == 0;
    }

    [[nodiscard]]
    auto Data() -> T *
    {
        return m_Data;
    }

    [[nodiscard]]
    auto Data() const -> const T *
    {
        return m_Data;
    }

    [[nodiscard]]
    auto CStr() const -> const T *
    {
        NYLA_DASSERT(*(Data() + Size()) == '\0');
        return m_Data;
    }

    [[nodiscard]]
    auto Size() const -> uint64_t
    {
        return m_Size;
    }

    [[nodiscard]]
    auto SizeBytes() const -> uint64_t
    {
        return m_Size * sizeof(T);
    }

    void ReSize(uint64_t newSize)
    {
        m_Size = newSize;
    }

    auto SubSpan(uint64_t from) -> Span
    {
        const uint64_t retSize = Size() - from;
        NYLA_DASSERT(retSize >= 0);
        return {Data() + from, retSize};
    }

    auto SubSpan(uint64_t from, uint64_t size) -> Span
    {
        NYLA_DASSERT(Size() - from >= size);
        return {Data() + from, size};
    }

    [[nodiscard]]
    auto begin() -> T *
    {
        return Data();
    }

    [[nodiscard]]
    auto begin() const -> const T *
    {
        return Data();
    }

    [[nodiscard]]
    auto cbegin() const -> const T *
    {
        return Data();
    }

    [[nodiscard]]
    auto end() -> T *
    {
        return Data() + Size();
    }

    [[nodiscard]]
    auto end() const -> const T *
    {
        return Data() + Size();
    }

    [[nodiscard]]
    auto cend() const -> const T *
    {
        return Data() + Size();
    }

    auto Front() -> T &
    {
        return (*this)[0];
    }

    [[nodiscard]]
    auto Front() const -> const T &
    {
        return (*this)[0];
    }

    auto Back() -> T &
    {
        return (*this)[m_Size - 1];
    }

    [[nodiscard]]
    auto Back() const -> const T &
    {
        return (*this)[m_Size - 1];
    }

    [[nodiscard]]
    auto operator==(Span rhs) const -> bool
    {
        if (this->Size() != rhs.Size())
            return false;

        if (this->Data() == rhs.Data())
            return true;
        else
            return MemEq((const char *)this->Data(), (const char *)rhs.Data(), rhs.SizeBytes());
    }

    [[nodiscard]]
    auto StartsWith(Span prefix) const -> bool
    {
        return MemStartsWith((const char *)this->Data(), this->SizeBytes(), (const char *)prefix.Data(),
                             prefix.SizeBytes());
    }

    [[nodiscard]]
    auto EndsWith(Span suffix) const -> bool
    {
        return MemEndsWith((const char *)this->Data(), this->SizeBytes(), (const char *)suffix.Data(),
                           suffix.SizeBytes());
    }
};

using Str = Span<const char>;
using ByteView = Span<const char>;

template <uint64_t N> consteval auto AsStr(const char (&str)[N]) -> Str
{
    return Str{str, N - 1};
}

template <typename T> auto AsStr(T str) -> Str
{
    return Str{str, CStrLen(str)};
}

template <typename T> constexpr auto AsStr(T str, uint64_t size) -> Str
{
    return Str{str, size};
}

template <typename T> auto ByteViewSpan(Span<T> in) -> ByteView
{
    return Span{(const char *)in.Data(), in.SizeBytes()};
}

template <typename T> constexpr auto ByteViewPtr(const T *in) -> ByteView
{
    return Span{(const char *)in, sizeof(T)};
}

//

template <typename T, uint64_t N> struct alignas(RequiredAlignment<T>::value) Array
{
    static_assert(std::is_trivially_constructible<T>());
    static_assert(std::is_trivially_destructible<T>());

    T data[N];

    auto GetSpan() -> Span<T>
    {
        return {Data(), Size()};
    }

    auto GetSpan() const -> Span<const T>
    {
        return {Data(), Size()};
    }

    [[nodiscard]]
    auto operator[](uint64_t i) -> T &
    {
        return data[i];
    }

    [[nodiscard]]
    auto operator[](uint64_t i) const -> const T &
    {
        return data[i];
    }

    [[nodiscard]]
    auto Empty() const -> bool
    {
        return Size() == 0;
    }

    [[nodiscard]]
    auto Data() -> T *
    {
        return data;
    }

    [[nodiscard]]
    auto Data() const -> const T *
    {
        return data;
    }

    [[nodiscard]]
    constexpr auto Size() const -> uint64_t
    {
        return N;
    }

    [[nodiscard]]
    constexpr auto Size32() const -> uint32_t
    {
        return static_cast<uint32_t>(Size());
    }

    [[nodiscard]]
    auto MaxSize() const -> uint64_t
    {
        return N;
    }

    [[nodiscard]]
    auto begin() -> T *
    {
        return Data();
    }

    [[nodiscard]]
    auto begin() const -> const T *
    {
        return Data();
    }

    [[nodiscard]]
    auto cbegin() const -> const T *
    {
        return Data();
    }

    [[nodiscard]]
    auto end() -> T *
    {
        return Data() + Size();
    }

    [[nodiscard]]
    auto end() const -> const T *
    {
        return Data() + Size();
    }

    [[nodiscard]]
    auto cend() const -> const T *
    {
        return Data() + Size();
    }
};

class ByteParser
{
  public:
    void Init(const char *base, uint64_t size)
    {
        m_At = base;
        m_Left = size;
    }

    constexpr auto Left() -> uint64_t
    {
        return m_Left;
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

    auto StartsWith(Str str) -> bool
    {
        return AsStr(m_At, m_Left).StartsWith(str);
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
        const ParseNumberResult res = ParseDecimal(d, l);
        NYLA_ASSERT(res == ParseNumberResult::Long);
        return l;
    }

    enum ParseNumberResult
    {
        Double,
        Long,
    };

    constexpr auto ParseDecimal(double &outDouble, int64_t &outLong) -> ParseNumberResult
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

  protected:
    const char *m_At;
    uint64_t m_Left;
};

//

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
    NYLA_ASSERT(str.Size() <= N);

    InlineString<N> ret{
        .m_Data = str.Data(),
        .m_Size = str.Size(),
    };
    return ret;
}

//

void NYLA_API StringWriteFmt(char *out, uint64_t outSize, Str fmt, ...);

template <uint64_t N> auto StringWriteFmt(InlineString<N> out, Str fmt, ...) -> Str
{
    va_list args;
    va_start(args, fmt);

    StringWriteFmt(out.Data(), out.Size(), fmt, args);

    va_end(args);

    return out.GetStr();
}

void NYLA_API FileWriteFmt(FileHandle handle, Str fmt, ...);

void NYLA_API FileWriteFmt(FileHandle handle, const char *fmt, uint64_t fmtSize, ...)
{
    va_list args;
    va_start(args, fmtSize);

    FileWriteFmt(handle, AsStr(fmt, fmtSize), args);

    va_end(args);
}

} // namespace nyla