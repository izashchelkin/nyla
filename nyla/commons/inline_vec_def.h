#pragma once

#include <cstdint>

#include "nyla/commons/array_def.h"
#include "nyla/commons/concepts.h"

namespace nyla
{

template <is_plain T, uint64_t Capacity> struct inline_vec
{
    array<T, Capacity> data;
    uint64_t size;

    [[nodiscard]]
    auto operator[](uint64_t i) -> T &;

    [[nodiscard]]
    auto operator[](uint64_t i) const -> const T &;

    [[nodiscard]]
    auto begin() -> T *
    {
        return data.begin();
    }

    [[nodiscard]]
    auto begin() const -> const T *
    {
        return data.begin();
    }

    [[nodiscard]]
    auto cbegin() const -> const T *
    {
        return data.cbegin();
    }

    [[nodiscard]]
    auto end() -> T *
    {
        return data.end();
    }

    [[nodiscard]]
    auto end() const -> const T *
    {
        return data.end();
    }

    [[nodiscard]]
    auto cend() const -> const T *
    {
        return data.cend();
    }

    operator span<T>()
    {
        return span<T>{data.data, size};
    }

    operator span<const T>() const
    {
        return span<const T>{data.data, size};
    }
};

} // namespace nyla