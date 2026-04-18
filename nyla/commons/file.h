#pragma once

#include "nyla/commons/bitenum.h"
#include "nyla/commons/byteliterals.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

using file_handle = void *;

constexpr inline uint64_t kPageSize = 4_KiB;

enum class FileOpenMode
{
    Read = 1 << 0,
    Write = 1 << 1,
    Append = 1 << 2,
};
NYLA_BITENUM(FileOpenMode);

auto API FileValid(file_handle file) -> bool;
auto API FileOpen(byteview path, FileOpenMode mode) -> file_handle;
void API FileClose(file_handle file);
auto API FileRead(file_handle file, uint32_t size, uint8_t *out) -> uint32_t;
auto API FileWrite(file_handle file, uint32_t size, const uint8_t *in) -> uint32_t;

enum class file_seek_mode
{
    Begin,
    Current,
    End
};

void API FileSeek(file_handle file, int64_t at, file_seek_mode mode);
auto API FileTell(file_handle file) -> uint64_t;

auto API GetStdin() -> file_handle;
auto API GetStdout() -> file_handle;
auto API GetStderr() -> file_handle;

} // namespace nyla