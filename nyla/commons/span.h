#pragma once

#include <concepts>
#include <cstdint>

#include "nyla/commons/mem.h"

namespace nyla
{

template <typename T> struct Span
{
    static_assert(std::is_trivially_constructible<T>());
    static_assert(std::is_trivially_destructible<T>());

    T *m_Data;
    uint64_t m_Size;

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
    auto Size() const -> uint64_t
    {
        return m_Size;
    }

    [[nodiscard]]
    auto ByteSize() const -> uint64_t
    {
        return m_Size * sizeof(T);
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

    [[nodiscard]]
    auto operator==(Span rhs) const -> bool
    {
        if (this->Size() != rhs.Size())
            return false;

        if (this->Data() == rhs.Data())
            return true;
        else
            return MemEq((const char *)this->Data(), (const char *)rhs.Data(), rhs.ByteSize());
    }

    [[nodiscard]]
    auto StartWith(Span prefix) const -> bool
    {
        return MemStartsWith((const char *)this->Data(), this->ByteSize(), (const char *)prefix.Data(),
                             prefix.ByteSize());
    }

    [[nodiscard]]
    auto EndsWith(Span suffix) const -> bool
    {
        return MemEndsWith((const char *)this->Data(), this->ByteSize(), (const char *)suffix.Data(),
                           suffix.ByteSize());
    }
};

using Str = Span<const char>;

template <uint64_t N> consteval auto AsStr(const char (&str)[N]) -> Str
{
    return Str{str, N - 1};
}

template <typename T>
auto AsStr(T str) -> Str
    requires(std::same_as<T, char *> || std::same_as<T, const char *>)
{
    return Str{str, CStrLen(str)};
}

} // namespace nyla