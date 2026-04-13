#pragma once

#include <cstdint>

#include "nyla/commons/concepts.h"

namespace nyla
{

template <is_plain T> struct span
{
    T *data;
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
        return data + size;
    }

    [[nodiscard]]
    auto end() const -> const T *
    {
        return data + size;
    }

    [[nodiscard]]
    auto cend() const -> const T *
    {
        return data + size;
    }

    operator span<const T>() const
    {
        return span<const T>{data, size};
    }
};

using byteview = span<const uint8_t>;

constexpr byteview operator""_s(const char *str, uint64_t len)
{
    return byteview{(uint8_t *)str, len};
}

} // namespace nyla