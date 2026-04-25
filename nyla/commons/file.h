#pragma once

#include "nyla/commons/bitenum.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/span_def.h"
#include <cstdint>

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

struct file_walker;

enum class file_attribute
{
    Readonly = 1 << 0,
    Hidden = 1 << 1,
    Directory = 1 << 2,
};
NYLA_BITENUM(file_attribute);

struct file_metadata
{
    byteview name;
    file_attribute attributes;
    uint64_t size;
    uint64_t creationTime;
    uint64_t lastAccessTime;
    uint64_t lastWriteTime;
};

namespace FileWalk
{

auto API FindFirst(region_alloc &alloc, byteview path, file_metadata &outMetadata) -> file_walker *;
auto API FindNext(file_walker &self, file_metadata &outMetadata) -> bool;
void API Close(file_walker &self);

} // namespace FileWalk

} // namespace nyla