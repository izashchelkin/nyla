#pragma once

#include <cstdint>

#include "nyla/commons/concepts.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/span.h"

namespace nyla
{

template <Plain T, uint64_t Size> struct alignas(RequiredAlignment<T>::value) array
{
    T data[Size];

    [[nodiscard]]
    auto operator[](uint64_t i) -> T &
    {
        NYLA_DASSERT(i < Size);
        return data[i];
    }

    [[nodiscard]]
    auto operator[](uint64_t i) const -> const T &
    {
        NYLA_DASSERT(i < Size);
        return data[i];
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

namespace Array
{

template <typename T, uint64_t Size>
[[nodiscard]]
INLINE auto Front(const array<T, Size> &self) -> T &
{
    return self[0];
}

template <typename T, uint64_t Size>
[[nodiscard]]
INLINE auto Back(const array<T, Size> &self) -> T &
{
    return self[Size - 1];
}

} // namespace Array

} // namespace nyla