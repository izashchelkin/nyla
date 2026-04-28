#include "nyla/commons/dir_watcher.h"

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/platform_dir_watch.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/span.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

namespace
{

struct watched_dir
{
    byteview path;
    platform_dir_watch *watch;
};

struct dir_subscriber
{
    byteview suffix;
    dir_watcher_callback cb;
    void *user;
};

struct dir_watcher
{
    region_alloc *alloc;
    inline_vec<watched_dir, 16> dirs;
    inline_vec<dir_subscriber, 32> subs;
};

dir_watcher *g_watcher;

} // namespace

namespace DirWatcher
{

void API Bootstrap(region_alloc &alloc)
{
    g_watcher = &RegionAlloc::Alloc<dir_watcher>(RegionAlloc::g_BootstrapAlloc);
    g_watcher->alloc = &alloc;
}

void API WatchDir(region_alloc &alloc, byteview path)
{
    ASSERT(g_watcher);

    span<uint8_t> pathCopy = RegionAlloc::AllocArray<uint8_t>(alloc, path.size + 1);
    MemCpy(pathCopy.data, path.data, path.size);
    pathCopy.data[path.size] = 0;
    byteview storedPath{pathCopy.data, path.size};

    platform_dir_watch *watch = PlatformDirWatch::Create(alloc, storedPath);
    ASSERT(watch);

    InlineVec::Append(g_watcher->dirs, watched_dir{
                                           .path = storedPath,
                                           .watch = watch,
                                       });

    LOG("dir_watcher: watching " SV_FMT, SV_ARG(storedPath));
}

void API Subscribe(byteview suffix, dir_watcher_callback cb, void *user)
{
    ASSERT(g_watcher);
    InlineVec::Append(g_watcher->subs, dir_subscriber{
                                           .suffix = suffix,
                                           .cb = cb,
                                           .user = user,
                                       });
}

void API Tick()
{
    if (!g_watcher)
        return;

    for (uint64_t i = 0; i < g_watcher->dirs.size; ++i)
    {
        watched_dir &dir = g_watcher->dirs[i];

        platform_dir_watch_event raw;
        while (PlatformDirWatch::Poll(*dir.watch, raw))
        {
            dir_watcher_event ev{
                .dirPath = dir.path,
                .name = raw.name,
                .mask = raw.mask,
            };

            for (uint64_t j = 0; j < g_watcher->subs.size; ++j)
            {
                dir_subscriber &sub = g_watcher->subs[j];
                if (Span::EndsWith(ev.name, sub.suffix))
                    sub.cb(ev, sub.user);
            }
        }
    }
}

} // namespace DirWatcher

} // namespace nyla
