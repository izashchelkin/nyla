#pragma once

#include <cstdint>

#include "nyla/commons/bitenum.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

using file_handle = void *;

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

enum class file_attribute
{
    Readonly = 1 << 0,
    Hidden = 1 << 1,
    Directory = 1 << 2,
};
NYLA_BITENUM(file_attribute);

struct file_metadata
{
    byteview fileName;
    file_attribute attributes;
    uint64_t fileSize;
    uint64_t creationTime;
    uint64_t lastAccessTime;
    uint64_t lastWriteTime;
};

struct dir_iter;

namespace DirIter
{

auto API Create(region_alloc &alloc, byteview path) -> dir_iter *;
auto API Next(region_alloc &alloc, dir_iter &self, file_metadata &out) -> bool;
void API Destroy(dir_iter &self);

}; // namespace DirIter

} // namespace nyla