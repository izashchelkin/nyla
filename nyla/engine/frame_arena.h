#pragma once

#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

namespace nyla
{

namespace engine0_internal
{

void FrameArenaInit();
auto FrameArenaAlloc(uint32_t bytes, uint32_t align) -> char *;

} // namespace engine0_internal

using namespace engine0_internal;

template <typename T, typename... Args> auto FrameArenaMake(Args &&...args) -> T &
{
    static_assert(std::is_trivially_destructible<T>() && std::is_trivially_constructible<T>());

    char *p = FrameArenaAlloc(sizeof(T), alignof(T));
    return *(::new (p) T(std::forward<Args>(args)...));
}

template <typename T> auto FrameArenaMove(T &&value) -> std::remove_cvref_t<T> &
{
    static_assert(std::is_trivially_destructible<T>() && std::is_trivially_constructible<T>());

    using U = std::remove_cvref_t<T>;
    return FrameArenaMake<U>(std::forward<T>(value));
}

template <typename T> auto FrameArenaMakeArray(uint32_t n) -> std::span<T>
{
    static_assert(std::is_trivially_destructible<T>() && std::is_trivially_constructible<T>());

    T *p = reinterpret_cast<T *>(FrameArenaAlloc(sizeof(T) * n, alignof(T)));
    for (uint32_t i = 0; i < n; ++i)
    {
        ::new (p + i) T;
    }
    return std::span{p, n};
}

} // namespace nyla