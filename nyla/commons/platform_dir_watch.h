#pragma once

#include <cstdint>

#include "nyla/commons/bitenum.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

struct platform_dir_watch;

enum class platform_dir_watch_event_type : uint8_t
{
    Modified = 1 << 0,
    Deleted = 1 << 1,
    MovedFrom = 1 << 2,
    MovedTo = 1 << 3,
};
NYLA_BITENUM(platform_dir_watch_event_type);

struct platform_dir_watch_event
{
    byteview name;
    platform_dir_watch_event_type mask;
};

namespace PlatformDirWatch
{

auto API Create(region_alloc &alloc, byteview path) -> platform_dir_watch *;
void API Destroy(platform_dir_watch &self);
auto API Poll(platform_dir_watch &self, platform_dir_watch_event &out) -> bool;

} // namespace PlatformDirWatch

} // namespace nyla
