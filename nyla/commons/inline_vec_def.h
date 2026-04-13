#pragma once

#include <cstdint>

#include "nyla/commons/array.h"
#include "nyla/commons/concepts.h"

namespace nyla
{

template <is_plain T, uint64_t Capacity> struct inline_vec
{
    array<T, sizeof(T) * Capacity> data;
    uint64_t size;

    [[nodiscard]]
    auto operator[](uint64_t i) -> T &;

    [[nodiscard]]
    auto operator[](uint64_t i) const -> const T &;

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
        return data + Capacity;
    }

    [[nodiscard]]
    auto end() const -> const T *
    {
        return data + Capacity;
    }

    [[nodiscard]]
    auto cend() const -> const T *
    {
        return data + Capacity;
    }

    operator span<T>()
    {
        return span<T>{data, size};
    }

    operator span<const T>() const
    {
        return span<const T>{data, size};
    }
};

} // namespace nyla
