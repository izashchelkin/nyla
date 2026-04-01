#include "nyla/commons/mem.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/intrin.h"

#include <cstdint>

namespace nyla
{

namespace internal_mem
{

auto NYLA_API CStrLen(const char *str) -> uint64_t
{
    // NYLA_ASSERT(reinterpret_cast<uintptr_t>(str) % 16 == 0);

    __m128i zero = _mm_setzero_si128();
    for (const char *ptr = str;; ptr += 16)
    {
        int cmp = _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)ptr), zero));
        if (cmp)
        {
            unsigned long index;
            NYLA_DASSERT(_BitScanForward(&index, cmp));
            return (ptr - str) + index;
        }
    }
}

} // namespace internal_mem

auto MemEq(const char *p1, const char *p2, uint64_t len) -> bool
{
    if (len < 16)
    {
        if (len >= 8)
        {
            if (Load64(p1) != Load64(p2))
                return false;
            return Load64(p1 + len - 8) == Load64(p2 + len - 8);
        }
        for (uint64_t i = 0; i < len; ++i)
        {
            if (p1[i] != p2[i])
                return false;
        }
        return true;
    }

    uint64_t blocks = len / 16;
    for (uint64_t i = 0; i < blocks; ++i)
    {
        __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(p1 + (i * 16)));
        __m128i v2 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(p2 + (i * 16)));
        __m128i cmp = _mm_cmpeq_epi8(v1, v2);

        if (_mm_movemask_epi8(cmp) != 0xFFFF)
            return false;
    }

    __m128i tail1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(p1 + len - 16));
    __m128i tail2 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(p2 + len - 16));
    __m128i tailCmp = _mm_cmpeq_epi8(tail1, tail2);

    return _mm_movemask_epi8(tailCmp) == 0xFFFF;
}

auto MemStartsWith(const char *str, uint64_t strLen, const char *prefix, uint64_t prefixLen) -> bool
{
    if (prefixLen > strLen)
        return false;
    else
        return MemEq(str, prefix, prefixLen);
}

auto MemEndsWith(const char *str, uint64_t strLen, const char *suffix, uint64_t suffixLen) -> bool
{
    if (suffixLen > strLen)
        return false;
    else
        return MemEq(str + strLen - suffixLen, suffix, suffixLen);
}

} // namespace nyla