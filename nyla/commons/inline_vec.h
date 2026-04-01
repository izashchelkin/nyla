#pragma once

#include <cstdint>

#include "nyla/commons/array.h"
#include "nyla/commons/span.h"

namespace nyla
{

template <typename T, uint64_t N> class InlineVec
{
  public:
    [[nodiscard]] auto Data() -> T *
    {
        return m_Data.Data();
    }

    [[nodiscard]] auto Data() const -> const T *
    {
        return m_Data.Data();
    }

    auto GetSpan() -> Span<T>
    {
        return {Data(), Size()};
    }

    auto GetSpan() const -> Span<const T>
    {
        return {Data(), Size()};
    }

    [[nodiscard]] auto Size() const -> uint64_t
    {
        return m_Size;
    }

    [[nodiscard]] auto Size32() const -> uint32_t
    {
        return m_Size;
    }

    [[nodiscard]] auto Empty() const -> bool
    {
        return m_Size == 0;
    }

    [[nodiscard]] constexpr auto MaxSize() const -> uint64_t
    {
        return N;
    }

    [[nodiscard]] auto operator[](uint64_t i) -> T &
    {
        NYLA_ASSERT(i < m_Size);
        return Data()[i];
    }

    [[nodiscard]] auto operator[](uint64_t i) const -> const T &
    {
        NYLA_ASSERT(i < m_Size);
        return Data()[i];
    }

    [[nodiscard]] auto begin() -> T *
    {
        return Data();
    }

    [[nodiscard]] auto begin() const -> const T *
    {
        return Data();
    }

    [[nodiscard]] auto cbegin() const -> const T *
    {
        return Data();
    }

    [[nodiscard]] auto end() -> T *
    {
        return Data() + m_Size;
    }

    [[nodiscard]] auto end() const -> const T *
    {
        return Data() + m_Size;
    }

    [[nodiscard]] auto cend() const -> const T *
    {
        return Data() + m_Size;
    }

    auto Front() -> T &
    {
        return (*this)[0];
    }

    auto Front() const -> const T &
    {
        return (*this)[0];
    }

    auto Back() -> T &
    {
        return (*this)[m_Size - 1];
    }

    auto Back() const -> const T &
    {
        return (*this)[m_Size - 1];
    }

    void Clear() noexcept
    {
        m_Size = 0;
    }

    template <class... Args> auto PushBack() -> T &
    {
        NYLA_ASSERT(m_Size < N);
        T *p = Data() + m_Size++;
        return *p;
    }

    template <class... Args> auto PushBack(const T &value) -> T &
    {
        NYLA_ASSERT(m_Size < N);
        T *p = Data() + m_Size++;
        *p = value;
        return *p;
    }

    void PopBack()
    {
        NYLA_ASSERT(m_Size);
        --m_Size;
    }

    void TakeBack(T &out)
    {
        out = Back();
    }

    void Resize(uint64_t newSize)
    {
        NYLA_ASSERT(newSize <= N);
        m_Size = newSize;
    }

    auto Erase(const T *pos) -> T *
    {
        NYLA_ASSERT(pos >= begin() && pos < end());
        return Erase(pos, pos + 1);
    }

    auto Erase(T *first, T *last) -> T *
    {
        if (first == last)
            return first;

        uint64_t numRemoved = last - first;
        uint64_t numToMove = end() - last;

        if (numToMove > 0)
            MemMove(first, last, numToMove * sizeof(T));

        m_Size -= numRemoved;
        return first;
    }

  private:
    uint64_t m_Size;
    Array<T, sizeof(T) * N> m_Data;
};

} // namespace nyla