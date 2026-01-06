#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

#include "nyla/commons/assert.h"

namespace nyla
{

template <typename T, uint32_t N> class InlineVec
{
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

    InlineVec() noexcept : m_Size(0)
    {
    }

    InlineVec(std::span<const value_type> elems) : m_Size(0)
    {
        NYLA_ASSERT(elems.size() <= kCapacity);
        for (const auto &e : elems)
            emplace_back(e);
    }

    InlineVec(const InlineVec &other) : m_Size(0)
    {
        for (size_type i = 0; i < other.m_Size; ++i)
            emplace_back(other[i]);
    }

    InlineVec(InlineVec &&other) noexcept(std::is_nothrow_move_constructible_v<T>) : m_Size(0)
    {
        for (size_type i = 0; i < other.m_Size; ++i)
            emplace_back(std::move(other[i]));
        other.clear();
    }

    auto operator=(const InlineVec &other) -> InlineVec &
    {
        if (this == &other)
            return *this;
        clear();
        for (size_type i = 0; i < other.m_Size; ++i)
            emplace_back(other[i]);
        return *this;
    }

    auto operator=(InlineVec &&other) noexcept(std::is_nothrow_move_constructible_v<T>) -> InlineVec &
    {
        if (this == &other)
            return *this;
        clear();
        for (size_type i = 0; i < other.m_Size; ++i)
            emplace_back(std::move(other[i]));
        other.clear();
        return *this;
    }

    ~InlineVec()
    {
        clear();
    }

    [[nodiscard]] auto data() -> pointer
    {
        return GetPtr();
    }
    [[nodiscard]] auto data() const -> const_pointer
    {
        return GetPtr();
    }

    [[nodiscard]] auto size() const -> size_type
    {
        return m_Size;
    }
    [[nodiscard]] auto empty() const -> bool
    {
        return m_Size == 0;
    }
    [[nodiscard]] constexpr auto max_size() const -> size_type
    {
        return kCapacity;
    }

    [[nodiscard]] auto operator[](size_type i) -> reference
    {
        NYLA_ASSERT(i < m_Size);
        return GetPtr()[i];
    }

    [[nodiscard]] auto operator[](size_type i) const -> const_reference
    {
        NYLA_ASSERT(i < m_Size);
        return GetPtr()[i];
    }

    [[nodiscard]] auto begin() -> iterator
    {
        return GetPtr();
    }
    [[nodiscard]] auto end() -> iterator
    {
        return GetPtr() + m_Size;
    }
    [[nodiscard]] auto begin() const -> const_iterator
    {
        return GetPtr();
    }
    [[nodiscard]] auto end() const -> const_iterator
    {
        return GetPtr() + m_Size;
    }
    [[nodiscard]] auto cbegin() const -> const_iterator
    {
        return GetPtr();
    }
    [[nodiscard]] auto cend() const -> const_iterator
    {
        return GetPtr() + m_Size;
    }

    auto front() -> reference
    {
        NYLA_ASSERT(m_Size);
        return GetPtr()[0];
    }

    auto front() const -> const_reference
    {
        NYLA_ASSERT(m_Size);
        return GetPtr()[0];
    }

    auto back() -> reference
    {
        NYLA_ASSERT(m_Size);
        return GetPtr()[m_Size - 1];
    }

    auto back() const -> const_reference
    {
        NYLA_ASSERT(m_Size);
        return GetPtr()[m_Size - 1];
    }

    void clear() noexcept
    {
        if constexpr (!std::is_trivially_destructible_v<T>)
            std::destroy_n(GetPtr(), m_Size);
        m_Size = 0;
    }

    void push_back(const_reference v)
    {
        (void)emplace_back(v);
    }
    void push_back(T &&v)
    {
        (void)emplace_back(std::move(v));
    }

    template <class... Args> auto emplace_back(Args &&...args) -> T &
    {
        NYLA_ASSERT(m_Size < kCapacity);
        T *p = GetPtr() + m_Size;
        std::construct_at(p, std::forward<Args>(args)...);
        ++m_Size;
        return *p;
    }

    void pop_back()
    {
        NYLA_ASSERT(m_Size);
        if constexpr (!std::is_trivially_destructible_v<T>)
            std::destroy_at(GetPtr() + (m_Size - 1));
        --m_Size;
    }

    auto TakeBack() -> value_type
    {
        static_assert(std::is_move_constructible_v<T>, "InlineVec::take_back requires T to be move constructible");

        NYLA_ASSERT(m_Size);
        value_type tmp = std::move(back());
        pop_back();
        return tmp;
    }

    void resize(size_type newSize)
    {
        NYLA_ASSERT(newSize <= kCapacity);

        if (newSize < m_Size)
        {
            if constexpr (!std::is_trivially_destructible_v<T>)
                std::destroy_n(GetPtr() + newSize, m_Size - newSize);
            m_Size = newSize;
            return;
        }

        if (newSize > m_Size)
        {
            static_assert(std::is_default_constructible_v<T>,
                          "InlineVec::resize(grow) requires T to be default constructible");
            while (m_Size < newSize)
                emplace_back();
        }
    }

  private:
    alignas(T) std::array<std::byte, sizeof(T) * kCapacity> m_Storage;
    size_type m_Size;

    auto GetPtr() -> T *
    {
        return reinterpret_cast<T *>(m_Storage.data());
    }
    auto GetPtr() const -> const T *
    {
        return reinterpret_cast<const T *>(m_Storage.data());
    }
};

} // namespace nyla