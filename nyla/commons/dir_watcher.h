#pragma once

#include "nyla/commons/macros.h"
#include "nyla/commons/platform_dir_watch.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

struct dir_watcher_event
{
    byteview dirPath;
    byteview name;
    platform_dir_watch_event_type mask;
};

using dir_watcher_callback = void (*)(const dir_watcher_event &event, void *user);

namespace DirWatcher
{

void API Bootstrap(region_alloc &alloc);
void API WatchDir(region_alloc &alloc, byteview path);
void API Subscribe(byteview suffix, dir_watcher_callback cb, void *user);
void API Tick();

} // namespace DirWatcher

} // namespace nyla
