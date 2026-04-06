#pragma once

#include <cstdint>
#include <type_traits>

namespace nyla
{

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

    template <typename K> auto Cast() const -> Span<K>
    {
        NYLA_ASSERT(!(sizeof(T) % sizeof(K)));
        return Span<K>{(K *)Data(), Size() * sizeof(T) / sizeof(K)};
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

} // namespace nyla