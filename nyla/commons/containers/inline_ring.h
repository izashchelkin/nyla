#pragma once

#include "nyla/commons/assert.h"

#include <array>
#include <cstdint>
#include <span>

namespace nyla
{

template <typename T, uint32_t N> class InlineRing
{
    static_assert(std::is_trivially_destructible_v<T>);

    static constexpr uint32_t kCapacity = N;

  private:
    void AdvanceWrite()
    {
        m_Write = (m_Write + 1) % kCapacity;
        NYLA_ASSERT(m_Write != m_Read);
    }

    void AdvanceRead()
    {
        m_Read = (m_Read + 1) % kCapacity;
    }

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

    InlineRing() = default;

    [[nodiscard]]
    auto empty() const -> bool
    {
        return m_Read == m_Write;
    }

    [[nodiscard]]
    constexpr auto max_size() const -> size_type
    {
        return kCapacity;
    }

    void clear()
    {
        m_Read = 0;
        m_Write = 0;
    }

    void push_back(const_reference v)
    {
        m_Data[m_Write] = v;
        AdvanceWrite();
    }

    void push_back(T &&v)
    {
        m_Data[m_Write] = std::move(v);
        AdvanceWrite();
    }

    template <class... Args> auto emplace_back(Args &&...args) -> T &
    {
        T &ret = (m_Data[m_Write] = T(std::forward<Args>(args)...));
        AdvanceWrite();
        return ret;
    }

    [[nodiscard]]
    auto pop_front() -> value_type
    {
        auto ret = m_Data[m_Read];
        AdvanceRead();
        return ret;
    }

  private:
    std::array<T, N> m_Data;
    size_type m_Write{};
    size_type m_Read{};
};

} // namespace nyla