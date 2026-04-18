#pragma once

#include <cstdint>
#include <type_traits>

#include "nyla/commons/concepts.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

template <is_plain T>
[[nodiscard]]
auto span<T>::operator[](uint64_t i) -> T &
{
    DASSERT(i < size);
    return data[i];
}

template <is_plain T>
[[nodiscard]]
auto span<T>::operator[](uint64_t i) const -> const T &
{
    DASSERT(i < size);
    return data[i];
}

namespace Span
{

template <typename T>
[[nodiscard]]
INLINE auto IsEmpty(span<T> self) -> bool
{
    return self.size == 0;
}

template <typename T>
[[nodiscard]]
INLINE auto CStr(span<T> self) -> const char *
    requires(sizeof(T) == 1)
{
    ASSERT(*(self.data + self.size) == 0);
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
    DASSERT(retSize >= 0);
    return {self.data + from, retSize};
}

template <typename T>
[[nodiscard]]
INLINE auto SubSpan(span<T> self, uint64_t from, uint64_t size) -> span<T>
{
    DASSERT(self.size - from >= size);
    return {self.data + from, size};
}

template <typename K, typename T>
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

INLINE auto FromCStr(const void *str, uint64_t maxLength) -> byteview
{
    return byteview{(uint8_t *)str, CStrLen(str, maxLength)};
}

template <typename T> INLINE auto ByteViewPtr(T *ptr) -> byteview
{
    return byteview{(uint8_t *)ptr, sizeof(T)};
}

template <typename T> INLINE auto Contains(const span<T> &self, const T &value) -> bool
{
    for (auto &elem : self)
    {
        if (elem == value)
            return true;
    }
    return false;
}

template <typename T> INLINE auto Erase(span<T> &self, T *first, T *last) -> T *
{
    static_assert(!std::is_const_v<T>);

    if (first == last)
        return first;

    uint64_t numRemoved = last - first;
    uint64_t numToMove = self.end() - last;

    if (numToMove > 0)
        MemMove(first, last, numToMove * sizeof(T));

    self.size -= numRemoved;
    return first;
}

template <typename T> INLINE auto Erase(span<T> &self, T *pos) -> T *
{
    DASSERT(pos >= self.begin() && pos < self.end());
    return Erase(self, pos, pos + 1);
}

template <typename T> INLINE void EraseIfEquals(span<T> &self, const T &value)
{
    for (uint64_t i = 0; i < self.size;)
    {
        T *ptr = self.data + i;
        if (*ptr == value)
            Erase(self, ptr);
        else
            ++i;
    }
}

} // namespace Span

} // namespace nyla