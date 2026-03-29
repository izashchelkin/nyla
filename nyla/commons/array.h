#pragma once

#include <cstdint>
#include <type_traits>

#include "nyla/commons/str.h"

namespace nyla
{

template <typename T, uint64_t N> class alignas(RequiredAlignment<T>::value) Array
{
    static_assert(std::is_trivially_constructible<T>());
    static_assert(std::is_trivially_destructible<T>());

  public:
    Array() : m_Data{}
    {
    }

    Array(const T &first)
        requires(N == 1)
        : m_Data{first}
    {
        m_Data[0] = first;
    }

    Array(const T &first, const T &second)
        requires(N == 2)
        : m_Data{first, second}
    {
    }

    Array(const T &first, const T &second, const T &third)
        requires(N == 3)
        : m_Data{first, second, third}
    {
    }

    Array(const T &first, const T &second, const T &third, const T &forth)
        requires(N == 4)
        : m_Data{first, second, third, forth}
    {
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
        return Size() == 0;
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
        return N;
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

  private:
    T m_Data[(sizeof(T) * N + RequiredAlignment<T>::value - 1) / sizeof(T)];
};

} // namespace nyla