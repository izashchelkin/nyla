#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "nyla/commons/intrin.h"
#include "nyla/commons/macros.h"

namespace nyla
{

template <typename T> struct RequiredAlignment
{
    static constexpr size_t value = (alignof(T) > 16) ? alignof(T) : 16;
};

template <typename T>
auto LoadU(const void *ptr) -> T
    requires(std::is_trivially_constructible_v<T> and std::is_trivially_copyable_v<T>)
{

#if defined(_MSC_VER)
    return *(const __unaligned T *)(ptr);
#else
    T val;
    __builtin_memcpy(&val, ptr, sizeof(T));
    return val;
#endif
};

inline auto Load64U(const void *ptr) -> uint64_t
{
    return LoadU<uint64_t>(ptr);
}

template <typename T>
void WriteU(void *ptr, const T &val)
    requires(std::is_trivially_constructible_v<T>() and std::is_trivially_copyable_v<T>)
{

#if defined(_MSC_VER)
    *(__unaligned T *)(ptr) = val;
#else
    __builtin_memcpy(ptr, &val, sizeof(T));
#endif
}

inline void MemCpy(void *RESTRICT dest, const void *RESTRICT src, uint64_t size)
{
    if (!size || dest == src)
        return;

#if defined(_MSC_VER)
    __movsb(static_cast<unsigned char *>(dest), static_cast<const unsigned char *>(src), size);
#else
    asm volatile("rep movsb" : "+D"(dest), "+S"(src), "+c"(size) : : "memory");
#endif
}

inline void MemSet(void *dest, uint8_t value, uint64_t size)
{
    if (!size)
        return;

#if defined(_MSC_VER)
    __stosb(static_cast<unsigned char *>(dest), value, size);
#else
    asm volatile("rep stosb" : "+D"(dest), "+c"(size) : "a"(value) : "memory");
#endif
}

inline void MemZero(void *dest, uint64_t size)
{
    MemSet(dest, 0, size);
}

template <typename T> void MemZero(T *dest)
{
    MemSet(dest, 0, sizeof(T));
}

inline void MemMove(void *dest, const void *src, uint64_t size)
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
void Swap(T &lhs, T &rhs)
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
auto NYLA_API CStrLen(T str) -> uint64_t
    requires(std::same_as<T, char *> || std::same_as<T, const char *>)
{
    return internal_mem::CStrLen(str);
}

} // namespace nyla