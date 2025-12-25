#pragma once

#include "absl/log/check.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

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

    InlineVec() = default;

    InlineVec(std::span<const value_type> elems) : m_size{static_cast<size_type>(elems.size())}
    {
        CHECK_LE(elems.size(), kCapacity);
        std::copy_n(elems.begin(), elems.size(), m_data.begin());
    }

    [[nodiscard]]
    auto data() const -> const_pointer
    {
        return m_data.data();
    }

    [[nodiscard]]
    auto data() -> pointer
    {
        return m_data.data();
    }

    [[nodiscard]]
    auto size() const -> size_type
    {
        return m_size;
    }

    [[nodiscard]]
    auto empty() const -> bool
    {
        return m_size == 0;
    }

    [[nodiscard]]
    constexpr auto max_size() const -> size_type
    {
        return kCapacity;
    }

    [[nodiscard]]
    auto operator[](size_type i) -> reference
    {
        CHECK_LT(i, m_size);
        return m_data[i];
    }

    [[nodiscard]]
    auto operator[](size_type i) const -> const_reference
    {
        CHECK_LT(i, m_size);
        return m_data[i];
    }

    [[nodiscard]]
    auto begin() -> iterator
    {
        return m_data.data();
    }

    [[nodiscard]]
    auto end() -> iterator
    {
        return m_data.data() + m_size;
    }

    [[nodiscard]]
    auto begin() const -> const_iterator
    {
        return m_data.data();
    }

    [[nodiscard]]
    auto end() const -> const_iterator
    {
        return m_data.data() + m_size;
    }

    [[nodiscard]]
    auto cbegin() const -> const_iterator
    {
        return m_data.data();
    }

    [[nodiscard]]
    auto cend() const -> const_iterator
    {
        return m_data.data() + m_size;
    }

    void clear()
    {
        m_size = 0;
    }

    void push_back(const_reference v)
    {
        CHECK_LT(m_size, kCapacity);
        m_data[m_size++] = v;
    }

    void push_back(T &&v)
    {
        CHECK_LT(m_size, kCapacity);
        m_data[m_size++] = std::move(v);
    }

    template <class... Args> auto emplace_back(Args &&...args) -> T &
    {
        CHECK_LT(m_size, kCapacity);
        return (m_data[m_size++] = T(std::forward<Args>(args)...));
    }

    void resize(size_type size)
    {
        m_size = size;
    }

  private:
    std::array<T, N> m_data{};
    size_type m_size = 0;
};

} // namespace nyla