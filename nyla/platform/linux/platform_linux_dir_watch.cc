#include "nyla/commons/bitenum.h"
#include "nyla/commons/assert.h"
#include "nyla/platform/platform_dir_watch.h"
#include <array>
#include <cstdint>
#include <linux/limits.h>
#include <string_view>
#include <sys/inotify.h>
#include <sys/types.h>
#include <unistd.h>

namespace nyla
{

class PlatformDirWatch::Impl
{
    static constexpr uint32_t kBufSize = sizeof(inotify_event) + NAME_MAX + 1;

  public:
    void Init(const char *path);
    void Destroy();
    auto Poll(PlatformDirWatchEvent &outChange) -> bool;

  private:
    alignas(inotify_event) std::array<std::byte, kBufSize> m_Buf;
    uint32_t m_BufPos;
    uint32_t m_BufLen;
    int m_InotifyFd;
};

void PlatformDirWatch::Impl::Init(const char *path)
{
    m_InotifyFd = inotify_init1(IN_NONBLOCK);
    NYLA_ASSERT(m_InotifyFd > 0);

    int wd = inotify_add_watch(m_InotifyFd, path, IN_MODIFY | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);
    NYLA_ASSERT(wd != -1);
}

void PlatformDirWatch::Impl::Destroy()
{
    if (m_InotifyFd)
        close(m_InotifyFd);
}

auto PlatformDirWatch::Impl::Poll(PlatformDirWatchEvent &outChange) -> bool
{
    for (;;)
    {
        NYLA_ASSERT(m_BufPos <= m_BufLen);
        if (!m_BufPos || m_BufPos == m_BufLen)
        {
            m_BufLen = read(m_InotifyFd, m_Buf.data(), m_Buf.size());
            if (m_BufLen <= 0)
                return false;
        }

        const auto *event = reinterpret_cast<inotify_event *>(m_Buf.data() + m_BufPos);
        m_BufPos += sizeof(inotify_event);

        if (event->mask & IN_ISDIR)
        {
            m_BufPos += outChange.name.size();
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

        outChange.name = (const char *)(m_Buf.data() + m_BufPos);
        m_BufPos += outChange.name.size();

        return true;
    }
}

//

void PlatformDirWatch::Init(const char *path)
{
    NYLA_ASSERT(!m_Impl);
    m_Impl = new PlatformDirWatch::Impl{};
    m_Impl->Init(path);
}

void PlatformDirWatch::Destroy()
{
    if (m_Impl)
    {
        m_Impl->Destroy();
        delete m_Impl;
    }
}

auto PlatformDirWatch::Poll(PlatformDirWatchEvent &outChange) -> bool
{
    return m_Impl->Poll(outChange);
}

} // namespace nyla