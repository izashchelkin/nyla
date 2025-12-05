#pragma once

#include <cstddef>
#include <span>
#include <utility>

namespace nyla
{

void TArenaInit();
auto TAlloc(size_t bytes, size_t align) -> void *;

template <typename T, typename... Args> auto Tnew(Args &&...args) -> T &
{
    void *p = TAlloc(sizeof(T), alignof(T));
    return *(::new (p) T(std::forward<Args>(args)...));
}

template <class T> auto Tmake(T &&value) -> std::remove_cvref_t<T> &
{
    using U = std::remove_cvref_t<T>;
    return Tnew<U>(std::forward<T>(value));
}

template <class T> auto Tmakearr(size_t n) -> std::span<T>
{
    T *p = reinterpret_cast<T *>(TAlloc(sizeof(T) * n, alignof(T)));
    for (size_t i = 0; i < n; ++i)
    {
        ::new (p + i) T;
    }
    return std::span{p, n};
}

} // namespace nyla