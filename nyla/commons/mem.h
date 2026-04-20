#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

#include "nyla/commons/byteliterals.h"
#include "nyla/commons/intrin.h" // IWYU pragma: keep
#include "nyla/commons/macros.h"

namespace nyla
{

constexpr inline uint64_t kPageSize = 4_KiB;

auto API ReserveMemPages(uint64_t size) -> void *;
void API CommitMemPages(void *page, uint64_t size);
void API DecommitMemPages(void *page, uint64_t size);

INLINE void MemCpy(void *RESTRICT dest, const void *RESTRICT src, uint64_t size)
{
#if 1
    memcpy(dest, src, size);
#else
    if (!size || dest == src)
        return;

#if defined(_MSC_VER)
    __movsb(static_cast<unsigned char *>(dest), static_cast<const unsigned char *>(src), size);
#else
    asm volatile("rep movsb" : "+D"(dest), "+S"(src), "+c"(size) : : "memory");
#endif
#endif
}

INLINE void MemSet(void *dest, uint8_t value, uint64_t size)
{
#if 1
    memset(dest, value, size);
#else
    if (!size)
        return;

#if defined(_MSC_VER)
    __stosb(static_cast<unsigned char *>(dest), value, size);
#else
    asm volatile("rep stosb" : "+D"(dest), "+c"(size) : "a"(value) : "memory");
#endif
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
#if 1
    memmove(dest, src, size);
#else
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
#endif
}

template <typename T>
INLINE void Swap(T &lhs, T &rhs)
    requires(std::is_trivially_constructible<T>())
{
    T tmp = lhs;
    lhs = rhs;
    rhs = tmp;
}

#if 1
[[nodiscard]]
auto INLINE MemEq(const void *p1, const void *p2, uint64_t len) -> bool
{
    return memcmp(p1, p2, len) == 0;
}
#else
[[nodiscard]]
auto API MemEq(const void *p1, const void *p2, uint64_t len) -> bool;
#endif

[[nodiscard]]
INLINE auto MemStartsWith(const void *str, uint64_t strLen, const void *prefix, uint64_t prefixLen) -> bool
{
    if (prefixLen > strLen)
        return false;
    else
        return MemEq(str, prefix, prefixLen);
}

[[nodiscard]]
INLINE auto MemEndsWith(const void *str, uint64_t strLen, const void *suffix, uint64_t suffixLen) -> bool
{
    if (suffixLen > strLen)
        return false;
    else
        return MemEq((const char *)str + strLen - suffixLen, suffix, suffixLen);
}

//

#if 1
[[nodiscard]]
INLINE auto CStrLen(const void *str, uint64_t maxLen) -> uint64_t
{
    uint8_t *p = (uint8_t *)memchr(str, 0, maxLen);
    ASSERT(p);
    return p - (uint8_t *)str;
}
#else
[[nodiscard]]
API auto CStrLen(const void *str, uint64_t maxLen) -> uint64_t;
#endif

} // namespace nyla