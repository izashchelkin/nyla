#pragma once

#include <concepts>
#include <cstdint>
#include <type_traits>

#include "nyla/commons/macros.h"

namespace nyla
{

INLINE void MemCpy(void *RESTRICT dest, const void *RESTRICT src, uint64_t size)
{
    if (!size || dest == src)
        return;

#if defined(_MSC_VER)
    __movsb(static_cast<unsigned char *>(dest), static_cast<const unsigned char *>(src), size);
#else
    asm volatile("rep movsb" : "+D"(dest), "+S"(src), "+c"(size) : : "memory");
#endif
}

INLINE void MemSet(void *dest, uint8_t value, uint64_t size)
{
    if (!size)
        return;

#if defined(_MSC_VER)
    __stosb(static_cast<unsigned char *>(dest), value, size);
#else
    asm volatile("rep stosb" : "+D"(dest), "+c"(size) : "a"(value) : "memory");
#endif
}

INLINE void MemZero(void *dest, uint64_t size)
{
    MemSet(dest, 0, size);
}

template <typename T> INLINE void MemZero(T *dest)
{
    MemSet(dest, 0, sizeof(T));
}

INLINE void MemMove(void *dest, const void *src, uint64_t size)
{
    if (!size || dest == src)
        return;

    auto d = static_cast<uint8_t *>(dest);
    auto s = static_cast<const uint8_t *>(src);

    if (d > s && d < s + size)
    {
#if defined(_MSC_VER)
        __movsb(d + size - 1, s + size - 1, size);
#else
        d += size;
        s += size;
        while (size--)
        {
            *(--d) = *(--s);
        }
#endif
    }
    else
    {
        MemCpy(dest, src, size);
    }
}

template <typename T>
INLINE void Swap(T &lhs, T &rhs)
    requires(std::is_trivially_constructible<T>())
{
    T tmp = lhs;
    lhs = rhs;
    rhs = tmp;
}

[[nodiscard]]
auto NYLA_API MemEq(const void *p1, const void *p2, uint64_t len) -> bool;
[[nodiscard]]
auto NYLA_API MemStartsWith(const void *str, uint64_t strLen, const void *prefix, uint64_t prefixLen) -> bool;
[[nodiscard]]
auto NYLA_API MemEndsWith(const void *str, uint64_t strLen, const void *suffix, uint64_t suffixLen) -> bool;

//

namespace internal_mem
{

auto NYLA_API CStrLen(const char *str) -> uint64_t;

} // namespace internal_mem

template <uint64_t N> consteval auto CStrLen(const char (&str)[N]) -> uint64_t
{
    return N - 1;
}

template <typename T>
auto INLINE CStrLen(T str) -> uint64_t
    requires(std::same_as<T, char *> || std::same_as<T, const char *> || std::same_as<T, uint8_t *> ||
             std::same_as<T, const uint8_t *>)
{
    return internal_mem::CStrLen(str);
}

} // namespace nyla