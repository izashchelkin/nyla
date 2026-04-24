#pragma once

#include "nyla/commons/span_def.h"

namespace nyla
{

namespace BinarySearch
{

// First index where proj(elem) >= key. Span must be sorted ascending by proj.
template <typename T, typename K, typename Proj> auto LowerBound(span<T> s, const K &key, Proj proj) -> uint64_t
{
    uint64_t lo = 0, hi = s.size;
    while (lo < hi)
    {
        uint64_t mid = lo + (hi - lo) / 2;
        if (proj(s[mid]) < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

// Pointer to element where proj(elem) == key, or nullptr.
template <typename T, typename K, typename Proj> auto Find(span<T> s, const K &key, Proj proj) -> T *
{
    uint64_t pos = LowerBound(s, key, proj);
    if (pos < s.size && proj(s[pos]) == key)
        return &s[pos];
    return nullptr;
}

} // namespace BinarySearch

} // namespace nyla
