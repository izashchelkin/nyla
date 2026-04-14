#pragma once

#include <cstdint>

#include "nyla/commons/file.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/span.h"

namespace nyla
{

template <is_plain T> INLINE void FileWrite(file_handle file, const T &data)
{
    ASSERT(FileWrite(file, sizeof(data), reinterpret_cast<const char *>(&data)) == sizeof(data));
}

template <is_plain T> INLINE void FileWriteSpan(file_handle file, span<T> data)
{
    uint64_t expectedSize = Span::SizeBytes(data);
    ASSERT(FileWrite(file, expectedSize, reinterpret_cast<const char *>(&data[0])) == expectedSize);
}

INLINE auto TryFileReadFully(region_alloc &alloc, file_handle file, span<uint8_t> &out) -> bool
{
    if (!FileValid(file))
        return false;

    uint64_t remaining = FileTell(file);
    out = RegionAlloc::AllocArray<uint8_t>(alloc, remaining);

    uint64_t read = 0;
    uint32_t n;
    while (remaining > 0 && (n = FileRead(file, remaining, out.data + read)))
    {
        read += n;
        remaining -= n;
    }

    return remaining == 0;
}

INLINE auto FileReadFully(region_alloc &alloc, file_handle file) -> span<uint8_t>
{
    span<uint8_t> out;
    ASSERT(TryFileReadFully(alloc, file, out));
    return out;
}

} // namespace nyla