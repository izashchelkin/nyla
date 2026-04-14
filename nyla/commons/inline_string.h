#pragma once

#include <cstdarg>
#include <cstdint>

#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

void API StringWriteFmt(span<uint8_t> out, byteview fmt, ...);

template <uint64_t Capacity> using inline_string = inline_vec<uint8_t, Capacity>;

namespace InlineString
{

template <uint64_t Capacity> INLINE void AppendSuffix(inline_string<Capacity> &self, byteview suffix)
{
    InlineVec::Append(self, suffix);
    if (self.size < Capacity)
        DASSERT((self.data + self.size) == '\0');
}

template <uint64_t Capacity> INLINE void RemoveSuffix(inline_string<Capacity> &self, uint64_t suffixLen)
{
    DASSERT(suffixLen <= self.size);
    self.size -= suffixLen;
    self.data + self.size = '\0';
}

template <uint64_t Capacity> [[nodiscard]] INLINE bool TryRemoveSuffix(inline_string<Capacity> &self, byteview suffix)
{
    if (MemEndsWith(self.data, self.size, suffix.data, suffix.size))
    {
        RemoveSuffix(self, suffix.size);
        return true;
    }
    else
        return false;
}

template <uint64_t Capacity> void AsciiToUpper(inline_string<Capacity> &self)
{
    for (uint32_t i = 0; i < self.size; ++i)
    {
        char &ch = self[i];
        if (ch >= 'a' && ch <= 'z')
            ch = ch - ('a' - 'A');
        else
            ch = ch;
    }
}

template <uint64_t Capacity>
[[nodiscard]] INLINE auto WriteFmt(inline_string<Capacity> &out, byteview fmt, ...) -> byteview
{
    va_list args;
    va_start(args, fmt);
    StringWriteFmt(out.data + out.size, Capacity - out.size, fmt, args);
    va_end(args);
    return out;
}

}; // namespace InlineString

} // namespace nyla