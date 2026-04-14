#include "nyla/commons/mem.h"

#include <cstdint> // IWYU pragma: keep

#include "nyla/commons/fmt.h"    // IWYU pragma: keep
#include "nyla/commons/intrin.h" // IWYU pragma: keep
#include "nyla/commons/macros.h" // IWYU pragma: keep

namespace nyla
{

#if 0

auto API CStrLen(const void *str, uint64_t maxLen) -> uint64_t
{
    const uint8_t *ptr = (uint8_t *)str;

    if (maxLen >= 16)
    {
        __m128i zero = _mm_setzero_si128();

        for (; maxLen >= 16; ptr += 16, maxLen -= 16)
        {
            int cmp = _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)ptr), zero));
            if (cmp)
                return (ptr - (uint8_t *)str) + BitScanForward32(cmp);
        }
    }

    while (maxLen-- > 0)
    {
        if ((*ptr) == 0)
            return ptr - ((uint8_t *)str);
        ++ptr;
    }

    TRAP();
    return 0;
}

auto MemEq(const void *p1, const void *p2, uint64_t len) -> bool
{
    if (p1 == p2)
        return true;

    if (len < 16)
    {
        if (len >= 8)
        {
            if (Load64U(p1) != Load64U(p2))
                return false;
            return Load64U((const char *)p1 + len - 8) == Load64U((const char *)p2 + len - 8);
        }
        for (uint64_t i = 0; i < len; ++i)
        {
            if (((const char *)p1)[i] != ((const char *)p2)[i])
                return false;
        }
        return true;
    }

    uint64_t blocks = len / 16;
    for (uint64_t i = 0; i < blocks; ++i)
    {
        __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>((const char *)p1 + (i * 16)));
        __m128i v2 = _mm_loadu_si128(reinterpret_cast<const __m128i *>((const char *)p2 + (i * 16)));
        __m128i cmp = _mm_cmpeq_epi8(v1, v2);

        if (_mm_movemask_epi8(cmp) != 0xFFFF)
            return false;
    }

    __m128i tail1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>((const char *)p1 + len - 16));
    __m128i tail2 = _mm_loadu_si128(reinterpret_cast<const __m128i *>((const char *)p2 + len - 16));
    __m128i tailCmp = _mm_cmpeq_epi8(tail1, tail2);

    return _mm_movemask_epi8(tailCmp) == 0xFFFF;
}

#endif

} // namespace nyla