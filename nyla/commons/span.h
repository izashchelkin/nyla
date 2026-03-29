#pragma once

#include <cstdint>

namespace nyla
{

template <typename T> class Span
{
    using Iterator = T *;
    using ConstIterator = const T *;

  public:
    Span(T *data, uint64_t size) : m_Data{data}, m_Size{size}
    {
    }

    Span() : Span{nullptr, 0ULL}
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
    T *m_Data;
    uint64_t m_Size;
};

} // namespace nyla