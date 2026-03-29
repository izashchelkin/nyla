#include <cstdint>

#include "nyla/commons/intrin.h"

namespace nyla
{

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
    __builtin_memcpy(ptr, &val, 8); // Safe here: size '8' is a compile-time constant!
#endif
}

inline void MemCpy(void *dest, const void *src, uint64_t size)
{
#if defined(_MSC_VER)
    __movsb(static_cast<unsigned char *>(dest), static_cast<const unsigned char *>(src), size);
#else
    asm volatile("rep movsb" : "+D"(dest), "+S"(src), "+c"(size) : : "memory");
#endif
}

inline void MemSet(void *dest, uint8_t value, uint64_t size)
{
#if defined(_MSC_VER)
    __stosb(static_cast<unsigned char *>(dest), value, size);
#else
    asm volatile("rep stosb" : "+D"(dest), "+c"(size) : "a"(value) : "memory");
#endif
}

auto CStrLen(const char *str) -> uint64_t;
auto MemEq(const char *p1, const char *p2, uint64_t len) -> bool;
auto MemStartsWith(const char *str, uint64_t strLen, const char *prefix, uint64_t prefixLen) -> bool;
auto MemEndsWith(const char *str, uint64_t strLen, const char *suffix, uint64_t suffixLen) -> bool;

} // namespace nyla