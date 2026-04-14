#pragma once

#include <cstdint>

#include "nyla/commons/align.h"
#include "nyla/commons/concepts.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

template <is_plain T, uint64_t Size> struct alignas(required_align_v<T>) array
{
    T data[Size];

    [[nodiscard]]
    auto operator[](uint64_t i) -> T &;

    [[nodiscard]]
    auto operator[](uint64_t i) const -> const T &;

    [[nodiscard]]
    constexpr auto operator-() const
    {
        array ret;
        for (uint32_t i = 0; i < Size; ++i)
            ret[i] = -ret[i];
        return ret;
    }

    [[nodiscard]]
    constexpr auto operator==(const array &rhs) const -> bool
    {
        for (uint32_t i = 0; i < Size; ++i)
        {
            if (this[i] != rhs[i])
                return false;
        }
        return true;
    }

    constexpr auto operator+=(const array &rhs) -> array &
    {
        for (uint32_t i = 0; i < Size; ++i)
            this[i] += rhs[i];
        return *this;
    }

    [[nodiscard]]
    constexpr auto operator+(const array &rhs) const -> array
    {
        array lhs = *this;
        return (lhs += rhs);
    }
    constexpr auto operator-=(const array &rhs) -> array &
    {
        for (uint32_t i = 0; i < Size; ++i)
            this[i] -= rhs[i];
        return *this;
    }

    [[nodiscard]]
    constexpr auto operator-(const array &rhs) const -> array
    {
        array lhs = *this;
        return (lhs -= rhs);
    }

    constexpr auto operator*=(auto scalar) -> array &
    {
        for (uint32_t i = 0; i < Size; ++i)
            this[i] *= static_cast<T>(scalar);
        return *this;
    }

    [[nodiscard]]
    constexpr auto operator*(auto scalar) const -> array
    {
        array lhs = *this;
        return (lhs *= scalar);
    }

    constexpr auto operator/=(auto scalar) -> array &
    {
        for (uint32_t i = 0; i < Size; ++i)
            this[i] /= static_cast<T>(scalar);
        return *this;
    }

    [[nodiscard]]
    constexpr auto operator/(auto scalar) const -> array
    {
        array lhs = *this;
        return (lhs /= scalar);
    }

    [[nodiscard]]
    auto begin() -> T *
    {
        return data;
    }

    [[nodiscard]]
    auto begin() const -> const T *
    {
        return data;
    }

    [[nodiscard]]
    auto cbegin() const -> const T *
    {
        return data;
    }

    [[nodiscard]]
    auto end() -> T *
    {
        return data + Size;
    }

    [[nodiscard]]
    auto end() const -> const T *
    {
        return data + Size;
    }

    [[nodiscard]]
    auto cend() const -> const T *
    {
        return data + Size;
    }

    operator span<T>()
    {
        return span<T>{data, Size};
    }

    operator span<const T>() const
    {
        return span<const T>{data, Size};
    }
};

} // namespace nyla