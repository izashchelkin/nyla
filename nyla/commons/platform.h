#pragma once

#include <cstdint>

#include "nyla/commons/array.h"
#include "nyla/commons/platform_base.h"
#include "nyla/commons/region_alloc.h"

namespace nyla
{

namespace Platform
{

void NYLA_API ParseStdArgs(Str *args, uint32_t len);

//

template <typename T> void FileWrite(FileHandle file, const T &data)
{
    NYLA_ASSERT(Platform::FileWrite(file, sizeof(data), reinterpret_cast<const char *>(&data)) == sizeof(data));
}

template <typename T> void FileWriteSpan(FileHandle file, Span<T> data)
{
    uint64_t expectedSize = sizeof(data[0]) * data.Size();
    NYLA_ASSERT(Platform::FileWrite(file, expectedSize, reinterpret_cast<const char *>(&data[0])) == expectedSize);
}

auto FileRead(RegionAlloc &alloc, Str path, uint32_t size) -> Span<char>
{
    auto file = Platform::FileOpen(path.CStr(), FileOpenMode::Read);
    NYLA_ASSERT(Platform::FileValid(file));

    Span buff = alloc.template PushArr<char>(size);
    uint32_t numRead = Platform::FileRead(file, size, buff.Data());
    NYLA_ASSERT(numRead == size);

    return buff;
}

} // namespace Platform

} // namespace nyla