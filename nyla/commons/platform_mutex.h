#pragma once

#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc_def.h"

namespace nyla
{

struct platform_mutex;

namespace PlatformMutex
{

auto API Create(region_alloc &alloc) -> platform_mutex *;
void API Destroy(platform_mutex &self);
void API Lock(platform_mutex &self);
void API Unlock(platform_mutex &self);

} // namespace PlatformMutex

} // namespace nyla
