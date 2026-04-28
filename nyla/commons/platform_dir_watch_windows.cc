#include "nyla/commons/platform_dir_watch.h"

#include <cstdint>

#include "nyla/commons/array_def.h"
#include "nyla/commons/cast.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/headers_windows.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/span.h"

namespace nyla
{

namespace
{

constexpr uint32_t kBufSize = 64 * 1024;
constexpr uint32_t kNameBufSize = 4 * 1024;

} // namespace

struct platform_dir_watch
{
    HANDLE hDir;
    OVERLAPPED overlapped;
    bool pending;
    uint32_t bufPos;
    uint32_t bufLen;
    array<uint8_t, kNameBufSize> nameUtf8;
    alignas(DWORD) array<uint8_t, kBufSize> buf;
};

namespace PlatformDirWatch
{

namespace
{

void IssueRead(platform_dir_watch &self)
{
    self.bufPos = 0;
    self.bufLen = 0;
    self.pending =
        ReadDirectoryChangesW(self.hDir, self.buf.data, kBufSize, FALSE,
                              FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
                              nullptr, &self.overlapped, nullptr);
}

} // namespace

auto API Create(region_alloc &alloc, byteview path) -> platform_dir_watch *
{
    auto &self = RegionAlloc::Alloc<platform_dir_watch>(alloc);

    self.hDir =
        CreateFileA(Span::CStr(path), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    ASSERT(self.hDir != INVALID_HANDLE_VALUE);

    self.overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ASSERT(self.overlapped.hEvent);

    IssueRead(self);
    return &self;
}

void API Destroy(platform_dir_watch &self)
{
    if (self.pending)
        CancelIoEx(self.hDir, &self.overlapped);
    if (self.overlapped.hEvent)
        CloseHandle(self.overlapped.hEvent);
    if (self.hDir && self.hDir != INVALID_HANDLE_VALUE)
        CloseHandle(self.hDir);
}

auto API Poll(platform_dir_watch &self, platform_dir_watch_event &out) -> bool
{
    for (;;)
    {
        if (!self.bufLen)
        {
            if (!self.pending)
                IssueRead(self);

            DWORD transferred = 0;
            if (!GetOverlappedResult(self.hDir, &self.overlapped, &transferred, FALSE))
                return false;

            self.pending = false;
            self.bufPos = 0;
            self.bufLen = transferred;

            if (!self.bufLen)
            {
                IssueRead(self);
                return false;
            }
        }

        const auto *info = reinterpret_cast<const FILE_NOTIFY_INFORMATION *>(self.buf.data + self.bufPos);

        out.mask = None<platform_dir_watch_event_type>();
        switch (info->Action)
        {
        case FILE_ACTION_MODIFIED:
            out.mask |= platform_dir_watch_event_type::Modified;
            break;
        case FILE_ACTION_REMOVED:
            out.mask |= platform_dir_watch_event_type::Deleted;
            break;
        case FILE_ACTION_RENAMED_OLD_NAME:
            out.mask |= platform_dir_watch_event_type::MovedFrom;
            break;
        case FILE_ACTION_ADDED:
        case FILE_ACTION_RENAMED_NEW_NAME:
            out.mask |= platform_dir_watch_event_type::MovedTo;
            break;
        }

        int outLen = WideCharToMultiByte(CP_UTF8, 0, info->FileName, info->FileNameLength / 2,
                                         (char *)self.nameUtf8.data, kNameBufSize, nullptr, nullptr);
        out.name = byteview{self.nameUtf8.data, (uint64_t)outLen};

        if (info->NextEntryOffset)
        {
            self.bufPos += info->NextEntryOffset;
        }
        else
        {
            self.bufLen = 0;
            IssueRead(self);
        }

        return true;
    }
}

} // namespace PlatformDirWatch

} // namespace nyla