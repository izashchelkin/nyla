#pragma once

#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc_def.h"

namespace nyla
{

struct platform_thread;

using platform_thread_fn = void (*)(void *userdata);

namespace PlatformThread
{

auto API Create(region_alloc &alloc, platform_thread_fn fn, void *userdata) -> platform_thread *;
void API Join(platform_thread &self);
void API SetName(platform_thread &self, const char *name);

} // namespace PlatformThread

} // namespace nyla
