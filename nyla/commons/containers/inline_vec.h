#pragma once

#include "absl/log/check.h"
#include <array>
#include <cstddef>

namespace nyla
{

template <typename T, size_t N> class InlineVec
{
    static constexpr size_t kCapacity = N;

  public:
    [[nodiscard]]
    auto data() const -> const T *
    {
        return m_data.data();
    }

    [[nodiscard]]
    auto data() -> T *
    {
        return m_data.data();
    }

    [[nodiscard]]
    auto size() const -> size_t
    {
        return m_size;
    }

    [[nodiscard]]
    auto empty() const -> bool
    {
        return m_size == 0;
    }

    [[nodiscard]]
    constexpr auto max_size() const -> size_t
    {
        return kCapacity;
    }

    [[nodiscard]]
    auto operator[](std::size_t i) -> T &
    {
        CHECK_LT(i, m_size);
        return m_data[i];
    }

    [[nodiscard]]
    auto operator[](std::size_t i) const -> const T &
    {
        CHECK_LT(i, m_size);
        return m_data[i];
    }

    [[nodiscard]]
    auto begin() -> T *
    {
        return m_data.data();
    }

    [[nodiscard]]
    auto end() -> T *
    {
        return m_data.data() + m_size;
    }

    [[nodiscard]]
    auto begin() const -> const T *
    {
        return m_data.data();
    }

    [[nodiscard]]
    auto end() const -> const T *
    {
        return m_data.data() + m_size;
    }

    [[nodiscard]]
    auto cbegin() -> const T *
    {
        return m_data.data();
    }

    [[nodiscard]]
    auto cend() -> const T *
    {
        return m_data.data() + m_size;
    }

    void clear()
    {
        m_size = 0;
    }

    void push_back(const T &v)
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

  private:
    std::array<T, N> m_data{};
    std::size_t m_size = 0;
};

} // namespace nyla