#pragma once

#include <cstdint>

#include "nyla/commons/concepts.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/mem.h"

namespace nyla
{

template <Plain T> struct span
{
    T *data;
    uint64_t size;

    [[nodiscard]]
    auto operator[](uint64_t i) -> T &
    {
        NYLA_DASSERT(i < size);
        return data[i];
    }

    [[nodiscard]]
    auto operator[](uint64_t i) const -> const T &
    {
        NYLA_DASSERT(i < size);
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
};

using byteview = span<const uint8_t>;

namespace Span
{

template <typename T>
[[nodiscard]]
INLINE auto AsConst(span<T> self) -> span<const T>
{
    return {self.data, self.size};
}

template <typename T>
[[nodiscard]]
INLINE auto Empty(span<T> self) -> bool
{
    return self.size == 0;
}

template <typename T>
[[nodiscard]]
INLINE auto CStr(span<T> self) -> const char *
    requires(sizeof(T) == 1)
{
    NYLA_ASSERT(*(self.data + self.size) == 0);
    return (const char *)self.data;
}

template <typename T>
[[nodiscard]]
INLINE auto SizeBytes(span<T> self) -> uint64_t
{
    return self.size * sizeof(T);
}

template <typename T> INLINE void Resize(span<T> self, uint64_t newSize)
{
    self.size = newSize;
}

template <typename T>
[[nodiscard]]
INLINE auto SubSpan(span<T> self, uint64_t from) -> span<T>
{
    const uint64_t retSize = self.size - from;
    NYLA_DASSERT(retSize >= 0);
    return {self.data + from, retSize};
}

template <typename T>
[[nodiscard]]
INLINE auto SubSpan(span<T> self, uint64_t from, uint64_t size) -> span<T>
{
    NYLA_DASSERT(self.size - from >= size);
    return {self.data + from, size};
}

template <typename T, typename K>
[[nodiscard]]
INLINE auto Cast(span<T> self) -> span<K>
{
    return span<K>{(K *)self.data, self.size * sizeof(T) / sizeof(K)};
}

template <typename T>
[[nodiscard]]
INLINE auto Front(span<T> self) -> T &
{
    return self[0];
}

template <typename T>
[[nodiscard]]
INLINE auto Back(span<T> self) -> T &
{
    return self[self.size - 1];
}

template <typename T>
[[nodiscard]]
INLINE auto Eq(span<T> self, span<T> another) -> bool
{
    if (self.size == another.size)
        return MemEq(self.data, another.data, SizeBytes(self));
    else
        return false;
}

template <typename T>
[[nodiscard]]
INLINE auto StartsWith(span<T> self, span<T> prefix) -> bool
{
    return MemStartsWith(self.data, Span::SizeBytes(self), prefix.data, Span::SizeBytes(prefix));
}

template <typename T>
[[nodiscard]]
INLINE auto EndsWith(span<T> self, span<T> suffix) -> bool
{
    return MemEndsWith(self.data, Span::SizeBytes(self), suffix.data, Span::SizeBytes(suffix));
}

} // namespace Span

template <uint64_t N>
[[nodiscard]]
INLINE auto StringLiteralAsView(const char (&str)[N]) -> byteview
{
    return byteview{(uint8_t *)str, N - 1};
}

} // namespace nyla