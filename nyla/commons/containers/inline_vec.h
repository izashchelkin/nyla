#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

#include "nyla/commons/assert.h"

namespace nyla
{

template <typename T, uint32_t N> class InlineVec
{
    static_assert(std::is_trivially_destructible_v<T>);

    static constexpr uint32_t kCapacity = N;

  public:
    using value_type = T;
    using size_type = uint32_t;
    using difference_type = int32_t;
    using reference = T &;
    using const_reference = const T &;
    using pointer = T *;
    using const_pointer = const T *;
    using iterator = T *;
    using const_iterator = const T *;

    InlineVec() : m_Size{0} {};

    InlineVec(std::span<const value_type> elems) : m_Size{static_cast<size_type>(elems.size())}
    {
        NYLA_ASSERT(elems.size() <= kCapacity);
        std::copy_n(elems.begin(), elems.size(), m_Data.begin());
    }

    [[nodiscard]]
    auto data() const -> const_pointer
    {
        return m_Data.data();
    }

    [[nodiscard]]
    auto data() -> pointer
    {
        return m_Data.data();
    }

    [[nodiscard]]
    auto size() const -> size_type
    {
        return m_Size;
    }

    [[nodiscard]]
    auto empty() const -> bool
    {
        return m_Size == 0;
    }

    [[nodiscard]]
    constexpr auto max_size() const -> size_type
    {
        return kCapacity;
    }

    [[nodiscard]]
    auto operator[](size_type i) -> reference
    {
        NYLA_ASSERT(i < m_Size);
        return m_Data[i];
    }

    [[nodiscard]]
    auto operator[](size_type i) const -> const_reference
    {
        NYLA_ASSERT(i < m_Size);
        return m_Data[i];
    }

    [[nodiscard]]
    auto begin() -> iterator
    {
        return m_Data.data();
    }

    [[nodiscard]]
    auto end() -> iterator
    {
        return m_Data.data() + m_Size;
    }

    [[nodiscard]]
    auto begin() const -> const_iterator
    {
        return m_Data.data();
    }

    [[nodiscard]]
    auto end() const -> const_iterator
    {
        return m_Data.data() + m_Size;
    }

    [[nodiscard]]
    auto cbegin() const -> const_iterator
    {
        return m_Data.data();
    }

    [[nodiscard]]
    auto cend() const -> const_iterator
    {
        return m_Data.data() + m_Size;
    }

    void clear()
    {
        m_Size = 0;
    }

    void push_back(const_reference v)
    {
        NYLA_ASSERT(m_Size < kCapacity);
        m_Data[m_Size++] = v;
    }

    void push_back(T &&v)
    {
        NYLA_ASSERT(m_Size < kCapacity);
        m_Data[m_Size++] = std::move(v);
    }

    template <class... Args> auto emplace_back(Args &&...args) -> T &
    {
        NYLA_ASSERT(m_Size < kCapacity);
        return (m_Data[m_Size++] = T(std::forward<Args>(args)...));
    }

    auto front() -> reference
    {
        NYLA_ASSERT(m_Size);
        return m_Data[0];
    }

    auto front() const -> const_reference
    {
        NYLA_ASSERT(m_Size);
        return m_Data[0];
    }

    auto back() -> reference
    {
        NYLA_ASSERT(m_Size);
        return m_Data[m_Size - 1];
    }

    auto back() const -> const_reference
    {
        NYLA_ASSERT(m_Size);
        return m_Data[m_Size - 1];
    }

    auto pop_back() -> value_type
    {
        NYLA_ASSERT(m_Size);
        --m_Size;
        auto tmp = m_Data[m_Size];
        m_Data[m_Size] = {};
        return tmp;
    }

    void resize(size_type size)
    {
        NYLA_ASSERT(size <= m_Data.size());
        m_Size = size;
    }

  private:
    std::array<T, N> m_Data;
    size_type m_Size;
};

} // namespace nyla