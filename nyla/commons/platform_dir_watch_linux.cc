#include "nyla/commons/platform_dir_watch.h"

#include <cstdint>
#include <linux/limits.h>
#include <sys/inotify.h>
#include <unistd.h>

#include "nyla/commons/array_def.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/span.h"

namespace nyla
{

namespace
{

constexpr uint32_t kBufSize = sizeof(inotify_event) + NAME_MAX + 1;

} // namespace

struct platform_dir_watch
{
    int fd;
    uint32_t bufPos;
    uint32_t bufLen;
    alignas(inotify_event) array<uint8_t, kBufSize> buf;
};

namespace PlatformDirWatch
{

auto API Create(region_alloc &alloc, byteview path) -> platform_dir_watch *
{
    auto &self = RegionAlloc::Alloc<platform_dir_watch>(alloc);

    self.fd = inotify_init1(IN_NONBLOCK);
    ASSERT(self.fd > 0);

    int wd = inotify_add_watch(self.fd, Span::CStr(path), IN_MODIFY | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);
    ASSERT(wd != -1);

    return &self;
}

void API Destroy(platform_dir_watch &self)
{
    if (self.fd)
        close(self.fd);
}

auto API Poll(platform_dir_watch &self, platform_dir_watch_event &out) -> bool
{
    for (;;)
    {
        ASSERT(self.bufPos <= self.bufLen);
        if (!self.bufPos || self.bufPos == self.bufLen)
        {
            ssize_t n = read(self.fd, self.buf.data, kBufSize);
            if (n <= 0)
            {
                self.bufPos = 0;
                self.bufLen = 0;
                return false;
            }
            self.bufPos = 0;
            self.bufLen = (uint32_t)n;
        }

        const auto *event = reinterpret_cast<const inotify_event *>(self.buf.data + self.bufPos);
        self.bufPos += sizeof(inotify_event);

        if (event->mask & IN_ISDIR)
        {
            self.bufPos += event->len;
            continue;
        }

        out.mask = None<platform_dir_watch_event_type>();
        if (event->mask & IN_MODIFY)
            out.mask |= platform_dir_watch_event_type::Modified;
        if (event->mask & IN_DELETE)
            out.mask |= platform_dir_watch_event_type::Deleted;
        if (event->mask & IN_MOVED_FROM)
            out.mask |= platform_dir_watch_event_type::MovedFrom;
        if (event->mask & IN_MOVED_TO)
            out.mask |= platform_dir_watch_event_type::MovedTo;

        const uint8_t *name = self.buf.data + self.bufPos;
        uint32_t nameLen = 0;
        while (nameLen < event->len && name[nameLen] != 0)
            ++nameLen;
        out.name = byteview{name, nameLen};

        self.bufPos += event->len;
        return true;
    }
}

} // namespace PlatformDirWatch

} // namespace nyla
