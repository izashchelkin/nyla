#pragma once

#include "nyla/commons/fmt.h"
#include "nyla/commons/platform_base.h"
#include "nyla/commons/span.h"

namespace nyla
{

namespace Platform
{

void NYLA_API ParseStdArgs(Str *args, uint32_t len);

//

template <typename T> static void FileWrite(FileHandle file, const T &data)
{
    NYLA_ASSERT(Platform::FileWrite(file, sizeof(data), reinterpret_cast<const char *>(&data)) == sizeof(data));
}

template <typename T> static void FileWriteSpan(FileHandle file, Span<T> data)
{
    uint64_t expectedSize = sizeof(data[0]) * data.Size();
    NYLA_ASSERT(Platform::FileWrite(file, expectedSize, reinterpret_cast<const char *>(&data[0])) == expectedSize);
}

} // namespace Platform

} // namespace nyla