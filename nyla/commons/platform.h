#pragma once

#include <cstdint>

#include "nyla/commons/array.h"
#include "nyla/commons/platform_base.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/span.h"

namespace nyla
{

namespace Platform
{

void NYLA_API ParseStdArgs(strview *args, uint32_t len);

//

template <Plain T> void FileWrite(FileHandle file, const T &data)
{
    NYLA_ASSERT(Platform::FileWrite(file, sizeof(data), reinterpret_cast<const char *>(&data)) == sizeof(data));
}

template <Plain T> void FileWriteSpan(FileHandle file, span<T> data)
{
    uint64_t expectedSize = Span::SizeBytes(data);
    NYLA_ASSERT(Platform::FileWrite(file, expectedSize, reinterpret_cast<const char *>(&data[0])) == expectedSize);
}

auto FileRead(RegionAlloc &alloc, strview path, uint32_t size) -> span<uint8_t>
{
    auto file = Platform::FileOpen(Span::CStr(path), FileOpenMode::Read);
    NYLA_ASSERT(Platform::FileValid(file));

    Span buff = alloc.template PushArr<char>(size);
    uint32_t numRead = Platform::FileRead(file, size, buff.Data());
    NYLA_ASSERT(numRead == size);

    return buff;
}

} // namespace Platform

} // namespace nyla