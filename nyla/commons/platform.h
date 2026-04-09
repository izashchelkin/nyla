#pragma once

#include <cstdint>

#include "nyla/commons/concepts.h"
#include "nyla/commons/platform_base.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/span.h"

namespace nyla
{

namespace Platform
{

void ParseStdArgs(byteview *args, uint32_t maxArgs);

//

template <is_plain T> INLINE void FileWrite(FileHandle file, const T &data)
{
    NYLA_ASSERT(Platform::FileWrite(file, sizeof(data), reinterpret_cast<const char *>(&data)) == sizeof(data));
}

template <is_plain T> INLINE void FileWriteSpan(FileHandle file, span<T> data)
{
    uint64_t expectedSize = Span::SizeBytes(data);
    NYLA_ASSERT(Platform::FileWrite(file, expectedSize, reinterpret_cast<const char *>(&data[0])) == expectedSize);
}

INLINE auto FileRead(region_alloc &alloc, byteview path, uint32_t size) -> span<uint8_t>
{
    auto file = Platform::FileOpen(Span::CStr(path), FileOpenMode::Read);
    NYLA_ASSERT(Platform::FileValid(file));

    uint8_t *data = RegionAlloc::Alloc(alloc, size, 1);
    uint32_t numRead = Platform::FileRead(file, size, data);
    NYLA_ASSERT(numRead == size);

    return {data, size};
}

} // namespace Platform

} // namespace nyla