#include "absl/log/check.h"
#include "nyla/commons/bitenum.h"
#include "nyla/platform/platform_dir_watch.h"
#include <array>
#include <cstdint>
#include <linux/limits.h>
#include <sys/inotify.h>
#include <sys/types.h>
#include <unistd.h>

namespace nyla
{

struct PlatformDirWatchState
{
    static constexpr uint32_t kBufSize = sizeof(inotify_event) + NAME_MAX + 1;

    alignas(inotify_event) std::array<std::byte, kBufSize> buf;
    uint32_t bufPos;
    uint32_t bufLen;
    int inotifyFd;
};

void PlatformDirWatch::Init()
{
    CHECK(!m_State);
    m_State = new PlatformDirWatchState();

    m_State->inotifyFd = inotify_init1(IN_NONBLOCK);
    CHECK_GT(m_State->inotifyFd, 0);
}

void PlatformDirWatch::Destroy()
{
    if (m_State)
    {
        close(m_State->inotifyFd);
        delete m_State;
    }
}

auto PlatformDirWatch::PollChange(PlatformDirWatchEvent &outChange) -> bool
{
    auto &buf = m_State->buf;
    auto &bufPos = m_State->bufPos;
    auto &bufLen = m_State->bufLen;

    for (;;)
    {
        CHECK_LE(bufPos, bufLen);
        if (!bufPos || bufPos == bufLen)
        {
            bufLen = read(m_State->inotifyFd, buf.data(), buf.size());
            if (bufLen <= 0)
                return false;
        }

        const auto *event = reinterpret_cast<inotify_event *>(buf.data() + bufPos);
        bufPos += sizeof(inotify_event);

        if (event->mask & IN_ISDIR)
        {
            bufPos += outChange.name.size();
            continue;
        }

        outChange.mask = None<PlatformDirWatchEventType>();
        if (event->mask & IN_MODIFY)
            outChange.mask |= PlatformDirWatchEventType::Modified;
        if (event->mask & IN_DELETE)
            outChange.mask |= PlatformDirWatchEventType::Deleted;
        if (event->mask & IN_MOVED_FROM)
            outChange.mask |= PlatformDirWatchEventType::MovedFrom;
        if (event->mask & IN_MOVED_TO)
            outChange.mask |= PlatformDirWatchEventType::MovedTo;

        outChange.name = (const char *)(buf.data() + bufPos);
        bufPos += outChange.name.size();

        return true;
    }
}

} // namespace nyla