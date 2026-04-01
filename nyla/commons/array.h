#pragma once

#include <cstdint>
#include <type_traits>

#include "nyla/commons/mem.h"

namespace nyla
{

template <typename T, uint64_t N> struct alignas(RequiredAlignment<T>::value) Array
{
    static_assert(std::is_trivially_constructible<T>());
    static_assert(std::is_trivially_destructible<T>());

    T data[N];

    [[nodiscard]]
    auto operator[](uint64_t i) -> T &
    {
        return data[i];
    }

    [[nodiscard]]
    auto operator[](uint64_t i) const -> const T &
    {
        return data[i];
    }

    [[nodiscard]]
    auto Empty() const -> bool
    {
        return Size() == 0;
    }

    [[nodiscard]]
    auto Data() -> T *
    {
        return data;
    }

    [[nodiscard]]
    auto Data() const -> const T *
    {
        return data;
    }

    [[nodiscard]]
    constexpr auto Size() const -> uint64_t
    {
        return N;
    }

    [[nodiscard]]
    constexpr auto Size32() const -> uint32_t
    {
        return static_cast<uint32_t>(Size());
    }

    [[nodiscard]]
    auto MaxSize() const -> uint64_t
    {
        return N;
    }

    [[nodiscard]]
    auto begin() -> T *
    {
        return Data();
    }

    [[nodiscard]]
    auto begin() const -> const T *
    {
        return Data();
    }

    [[nodiscard]]
    auto cbegin() const -> const T *
    {
        return Data();
    }

    [[nodiscard]]
    auto end() -> T *
    {
        return Data() + Size();
    }

    [[nodiscard]]
    auto end() const -> const T *
    {
        return Data() + Size();
    }

    [[nodiscard]]
    auto cend() const -> const T *
    {
        return Data() + Size();
    }
};

} // namespace nyla