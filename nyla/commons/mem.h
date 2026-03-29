#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "nyla/commons/dllapi.h"
#include "nyla/commons/intrin.h"

namespace nyla
{

template <typename T> struct RequiredAlignment
{
    static_assert(alignof(std::max_align_t) == 16);

    static constexpr size_t kValue = (alignof(T) > 16) ? alignof(T) : 16;
};

inline auto Load64(const void *ptr) -> uint64_t
{
#if defined(_MSC_VER)
    return *reinterpret_cast<const __unaligned uint64_t *>(ptr);
#else
    uint64_t val;
    __builtin_memcpy(&val, ptr, 8);
    return val;
#endif
}

inline void Write64(void *ptr, uint64_t val)
{
#if defined(_MSC_VER)
    *reinterpret_cast<__unaligned uint64_t *>(ptr) = val;
#else
    __builtin_memcpy(ptr, &val, 8);
#endif
}

inline void MemCpy(void *dest, const void *src, uint64_t size)
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

template <typename To, typename From>
[[nodiscard]] inline auto BitCast(const From &src) -> To
    requires(sizeof(To) == sizeof(From))
{
    To dest;
    __builtin_memcpy(&dest, &src, sizeof(To));
    return dest;
}

template <uint64_t N> auto CStrLen(const char str[N]) -> uint64_t
{
    return N;
}
auto NYLA_API CStrLen(const char *str) -> uint64_t;

auto NYLA_API MemEq(const char *p1, const char *p2, uint64_t len) -> bool;
auto NYLA_API MemStartsWith(const char *str, uint64_t strLen, const char *prefix, uint64_t prefixLen) -> bool;
auto NYLA_API MemEndsWith(const char *str, uint64_t strLen, const char *suffix, uint64_t suffixLen) -> bool;

} // namespace nyla