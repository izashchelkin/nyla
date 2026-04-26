#include "nyla/commons/file.h"

#include <cstdint>
#include <immintrin.h>

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/cast.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/gamepad.h"
#include "nyla/commons/inline_queue.h"
#include "nyla/commons/intrin.h"
#include "nyla/commons/keyboard.h"
#include "nyla/commons/limits.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/platform_windows.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/span.h"
#include "nyla/commons/time.h"
#include "nyla/commons/wchar_conversions_windows.h"

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

struct dir_iter
{
    HANDLE hDir;
    span<uint8_t> buffer;
    FILE_ID_EXTD_DIR_INFO *fileInfo;
};

namespace DirIter
{

auto API Create(region_alloc &alloc, byteview path) -> dir_iter *
{
    void *allocMark = alloc.at;
    auto &self = RegionAlloc::Alloc<dir_iter>(alloc);

    self.hDir =
        CreateFileA(Span::CStr(path), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (self.hDir == INVALID_HANDLE_VALUE)
    {
        RegionAlloc::Reset(alloc, allocMark);
        return nullptr;
    }

    self.buffer = RegionAlloc::AllocArray<uint8_t>(alloc, (uint64_t)64 * 1024);

    if (!GetFileInformationByHandleEx(self.hDir, FileIdExtdDirectoryRestartInfo, self.buffer.data, self.buffer.size))
    {
        Destroy(self);

        RegionAlloc::Reset(alloc, allocMark);
        return nullptr;
    }

    self.fileInfo = (FILE_ID_EXTD_DIR_INFO *)self.buffer.data;

    return &self;
}

auto API Next(region_alloc &alloc, dir_iter &self, file_metadata &out) -> bool
{
    auto &fileInfo = self.fileInfo;

    if (fileInfo)
    {
        MemZero(&out);
        out.creationTime = fileInfo->CreationTime.QuadPart;
        out.lastAccessTime = fileInfo->LastAccessTime.QuadPart;
        out.lastWriteTime = fileInfo->LastWriteTime.QuadPart;
        out.fileSize = fileInfo->EndOfFile.QuadPart;
        out.fileName = ConvertWideCharToUTF8(alloc, {fileInfo->FileName, fileInfo->FileNameLength / 2});

        if (fileInfo->FileAttributes & FILE_ATTRIBUTE_READONLY)
            out.attributes |= file_attribute::Readonly;
        if (fileInfo->FileAttributes & FILE_ATTRIBUTE_HIDDEN)
            out.attributes |= file_attribute::Hidden;
        if (fileInfo->FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            out.attributes |= file_attribute::Directory;

        if (fileInfo->NextEntryOffset)
            fileInfo = (FILE_ID_EXTD_DIR_INFO *)((uint8_t *)fileInfo + fileInfo->NextEntryOffset);
        else
            fileInfo = nullptr;

        return true;
    }

    if (GetFileInformationByHandleEx(self.hDir, FileIdExtdDirectoryInfo, self.buffer.data, self.buffer.size))
    {
        return Next(alloc, self, out);
    }

    DWORD error = GetLastError();
    if (error == ERROR_NO_MORE_FILES)
        return false;

    ASSERT(false, "Failed to list files, error %d", GetLastError());
}

void API Destroy(dir_iter &self)
{
    CloseHandle(self.hDir);
}

}; // namespace DirIter

} // namespace nyla