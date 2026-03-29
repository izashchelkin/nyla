#pragma once

#include <cstdint>

namespace nyla
{

template <typename T, uint64_t N> class Array
{
    using Iterator = T *;
    using ConstIterator = const T *;

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
        return Size();
    }

    [[nodiscard]]
    auto begin() const -> ConstIterator
    {
        return Data();
    }

    [[nodiscard]]
    auto cbegin() const -> ConstIterator
    {
        return Data();
    }

    [[nodiscard]]
    auto end() const -> ConstIterator
    {
        return Data() + Size();
    }

    [[nodiscard]]
    auto cend() const -> ConstIterator
    {
        return Data() + Size();
    }

  private:
    T m_Data[N];
};

} // namespace nyla