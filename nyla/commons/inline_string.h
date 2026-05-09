#pragma once

#include <cstdint>

#include "nyla/commons/inline_vec.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/minmax.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

template <uint64_t Capacity> using inline_string = inline_vec<uint8_t, Capacity>;

namespace InlineString
{

template <uint64_t Capacity> INLINE void Assign(inline_string<Capacity> &self, byteview src)
{
    uint64_t n = Min(src.size, Capacity);
    if (n)
        MemCpy(self.data.data, src.data, n);

    self.size = n;
}

template <uint64_t Capacity> INLINE void AppendSuffix(inline_string<Capacity> &self, byteview suffix)
{
    InlineVec::Append(self, suffix);
}

template <uint64_t Capacity> void AsciiToUpper(inline_string<Capacity> &self)
{
    for (uint32_t i = 0; i < self.size; ++i)
    {
        uint8_t &ch = self[i];

        if (ch >= 'a' && ch <= 'z')
            ch = ch - ('a' - 'A');
    }
}

}; // namespace InlineString

} // namespace nyla