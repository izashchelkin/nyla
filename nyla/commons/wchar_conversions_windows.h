#include <cstdint>
#include <immintrin.h>

#include "nyla/commons/cast.h"

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/macros.h"
#include "nyla/commons/platform_windows.h" // IWYU pragma: keep
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"

namespace nyla
{

INLINE auto ConvertWideCharToUTF8(region_alloc &alloc, span<const wchar_t> src) -> byteview
{
    int outLen = WideCharToMultiByte(CP_UTF8, 0, src.data, CastI32(src.size), nullptr, 0, nullptr, nullptr);
    span out = RegionAlloc::AllocArray<uint8_t>(alloc, outLen);
    WideCharToMultiByte(CP_UTF8, 0, src.data, CastI32(src.size), (char *)out.data, outLen, nullptr, nullptr);

    return out;
}

INLINE auto ConvertUTF8ToWideChar(region_alloc &alloc, byteview src) -> span<const wchar_t>
{
    int outLen = MultiByteToWideChar(CP_UTF8, 0, (const char *)src.data, CastI32(src.size), nullptr, 0);
    span out = RegionAlloc::AllocArray<wchar_t>(alloc, outLen);
    MultiByteToWideChar(CP_UTF8, 0, (const char *)src.data, CastI32(src.size), out.data, outLen);

    return out;
}

} // namespace nyla