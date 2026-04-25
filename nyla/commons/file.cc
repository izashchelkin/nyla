#include "nyla/commons/platform_windows.h"

#include <cstdint>
#include <immintrin.h>

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/file.h"
#include "nyla/commons/gamepad.h"
#include "nyla/commons/inline_queue.h"
#include "nyla/commons/intrin.h"
#include "nyla/commons/keyboard.h"
#include "nyla/commons/limits.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/span.h"
#include "nyla/commons/time.h"

namespace nyla
{

auto API FileValid(file_handle file) -> bool
{
    auto hFile = reinterpret_cast<HANDLE>(file);
    return hFile != nullptr && hFile != INVALID_HANDLE_VALUE;
}

auto API FileOpen(byteview path, FileOpenMode mode) -> file_handle
{
    DWORD dwDesiredAccess = 0;
    DWORD dwCreationDisposition = 0;

    if (Any(mode & FileOpenMode::Read))
    {
        dwDesiredAccess |= GENERIC_READ;
        dwCreationDisposition = OPEN_EXISTING;
    }

    if (Any(mode & FileOpenMode::Append))
    {
        dwDesiredAccess |= GENERIC_WRITE;
        dwCreationDisposition = OPEN_ALWAYS;
    }
    if (Any(mode & FileOpenMode::Write))
    {
        dwDesiredAccess |= GENERIC_WRITE;
        dwCreationDisposition = CREATE_ALWAYS;
    }

    auto hFile = CreateFileA(Span::CStr(path), // lpFileName
                             dwDesiredAccess,
                             FILE_SHARE_READ, // dwShareMode
                             nullptr,         // lpSecurityAttributes
                             dwCreationDisposition,
                             FILE_ATTRIBUTE_NORMAL, // dwFlagsAndAttributes
                             nullptr                // hTemplateFile
    );

    if (hFile && hFile != INVALID_HANDLE_VALUE)
    {
        if (Any(mode & FileOpenMode::Append))
        {
            LARGE_INTEGER distanceToMove{};
            SetFilePointerEx(hFile, distanceToMove, nullptr, FILE_END);
        }
    }

    return reinterpret_cast<void *>(hFile);
}

void API FileClose(file_handle file)
{
    if (!FileValid(file))
        return;

    auto hFile = reinterpret_cast<HANDLE>(file);
    CloseHandle(hFile);
}

auto API FileRead(file_handle file, uint32_t size, uint8_t *out) -> uint32_t
{
    auto hFile = reinterpret_cast<HANDLE>(file);
    DWORD bytesRead = 0;

    ReadFile(hFile,      // hFile
             out,        // lpBuffer
             size,       // nNumberOfBytesToRead
             &bytesRead, // lpNumberOfBytesRead
             nullptr     // lpOverlapped
    );
    return bytesRead;
}

auto API FileWrite(file_handle file, uint32_t size, const uint8_t *in) -> uint32_t
{
    auto hFile = reinterpret_cast<HANDLE>(file);
    DWORD bytesWritten = 0;

    WriteFile(hFile,         // hFile
              in,            // lpBuffer
              size,          // nNumberOfBytesToWrite
              &bytesWritten, // lpNumberOfBytesWritten
              nullptr        // lpOverlapped
    );
    return bytesWritten;
}

void API FileSeek(file_handle file, int64_t at, file_seek_mode mode)
{
    auto hFile = reinterpret_cast<HANDLE>(file);

    LARGE_INTEGER distanceToMove;
    distanceToMove.QuadPart = at;

    DWORD moveMethod;
    switch (mode)
    {
    case file_seek_mode::Begin:
        moveMethod = FILE_BEGIN;
        break;
    case file_seek_mode::Current:
        moveMethod = FILE_CURRENT;
        break;
    case file_seek_mode::End:
        moveMethod = FILE_END;
        break;
    }

    SetFilePointerEx(hFile,          // hFile
                     distanceToMove, // lDistanceToMove
                     nullptr,        // lpDistanceToMoveHigh
                     moveMethod);
}

auto API FileTell(file_handle file) -> uint64_t
{
    auto hFile = reinterpret_cast<HANDLE>(file);

    LARGE_INTEGER distanceToMove{};
    LARGE_INTEGER newFilePointer{};

    if (SetFilePointerEx(hFile, distanceToMove, &newFilePointer, FILE_CURRENT))
    {
        return newFilePointer.QuadPart;
    }
    else
    { // failed
        ASSERT(false);
        return 0ULL;
    }
}

struct file_walker
{
    HANDLE handle;
    WIN32_FIND_DATAA data;
};

namespace FileWalk
{

namespace
{

void SetFileMetadata(WIN32_FIND_DATAA &in, file_metadata &out)
{
    MemZero(&out);
    out.name = byteview{(uint8_t *)in.cFileName, CStrLen(in.cFileName, MAX_PATH)};
    out.size = ((uint64_t)in.nFileSizeHigh << 32) | in.nFileSizeLow;
    out.creationTime = *(uint64_t *)&in.ftCreationTime;
    out.lastAccessTime = *(uint64_t *)&in.ftLastAccessTime;
    out.lastWriteTime = *(uint64_t *)&in.ftLastWriteTime;

    if (in.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
        out.attributes |= file_attribute::Readonly;
    if (in.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
        out.attributes |= file_attribute::Hidden;
    if (in.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        out.attributes |= file_attribute::Directory;
}

}; // namespace

auto API FindFirst(region_alloc &alloc, byteview path, file_metadata &outMetadata) -> file_walker *
{
    void *allocMark = alloc.at;
    file_walker &self = RegionAlloc::Alloc<file_walker>(alloc);
    self.handle = FindFirstFileA(Span::CStr(path), &self.data);

    if (self.handle == INVALID_HANDLE_VALUE)
    {
        RegionAlloc::Reset(alloc, allocMark);
        return nullptr;
    }
    else
    {
        SetFileMetadata(self.data, outMetadata);
        return &self;
    }
}

auto API FindNext(file_walker &self, file_metadata &outMetadata) -> bool
{
    if (FindNextFileA(self.handle, &self.data))
    {
        SetFileMetadata(self.data, outMetadata);
        return true;
    }

    return false;
}

void API Close(file_walker &self)
{
    FindClose(self.handle);
}

} // namespace FileWalk

} // namespace nyla