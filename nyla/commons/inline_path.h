#pragma once

#include <cstdint>

#include "nyla/commons/inline_string.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/macros.h"

namespace nyla
{

constexpr inline uint8_t kPathSep = '/';

namespace InlinePath
{

template <uint64_t Capacity> INLINE void Append(inline_string<Capacity> &self, byteview suffix)
{
    if (self.size > 0 && InlineVec::Back(self) != kPathSep)
        InlineString::AppendSuffix(self, kPathSep);

    InlineString::AppendSuffix(self, suffix);
}

template <uint64_t Capacity> INLINE auto PopBack(inline_string<Capacity> &self) -> byteview
{
    if (!self.size)
        return {};

    for (uint64_t i = self.size - 1; i >= 0; --i)
    {
        if (self[i] == kPathSep)
        {
            byteview ret{.data = &self[i + 1], .size = self.size - i - 1};
            self.size = i + 1;
            return ret;
        }
    }

    self.size = 0;
    return self;
}

template <uint64_t Capacity> INLINE auto TrySetFileExtension(inline_string<Capacity> &self, byteview ext) -> bool
{
    if (!self.size)
        return false;
    if (InlineVec::Back(self) == kPathSep)
        return false;

    for (uint64_t i = self.size - 1; i >= 0; --i)
    {
        switch (self[i])
        {
        case kPathSep: {
            InlineString::AppendSuffix(self, ext);
            return true;
        }

        case '.': {
            self.size = i;
            InlineString::AppendSuffix(self, ext);
            return true;
        }
        }
    }

    InlineString::AppendSuffix(self, ext);
    return true;
}

} // namespace InlinePath

} // namespace nyla