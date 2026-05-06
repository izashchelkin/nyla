#include "nyla/commons/dir_watcher.h"

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/macros.h"
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
    inline_vec<watched_dir, 16> dirs;
    inline_vec<dir_subscriber, 32> subs;
};

dir_watcher *manager;

} // namespace

namespace DirWatcher
{

void API Bootstrap()
{
    manager = &RegionAlloc::Alloc<dir_watcher>(RegionAlloc::g_BootstrapAlloc);
}

void API WatchDir(region_alloc &alloc, byteview path)
{
    byteview pathCopy = RegionAlloc::CopyByteViews(alloc, RegionAlloc::cstr_term, path);

    platform_dir_watch *watch = PlatformDirWatch::Create(alloc, pathCopy);
    ASSERT(watch);

    InlineVec::Append(manager->dirs, watched_dir{
                                         .path = pathCopy,
                                         .watch = watch,
                                     });

    LOG("dir_watcher: watching " SV_FMT, SV_ARG(pathCopy));
}

void API Subscribe(byteview suffix, dir_watcher_callback cb, void *user)
{
    InlineVec::Append(manager->subs, dir_subscriber{
                                         .suffix = suffix,
                                         .cb = cb,
                                         .user = user,
                                     });
}

void API Tick()
{
    for (uint64_t i = 0; i < manager->dirs.size; ++i)
    {
        watched_dir &dir = manager->dirs[i];

        platform_dir_watch_event raw;
        while (PlatformDirWatch::Poll(*dir.watch, raw))
        {
            dir_watcher_event ev{
                .dirPath = dir.path,
                .name = raw.name,
                .mask = raw.mask,
            };

            for (uint64_t j = 0; j < manager->subs.size; ++j)
            {
                dir_subscriber &sub = manager->subs[j];
                if (Span::EndsWith(ev.name, sub.suffix))
                    sub.cb(ev, sub.user);
            }
        }
    }
}

} // namespace DirWatcher

} // namespace nyla
