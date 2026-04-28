#pragma once

#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc_def.h"

namespace nyla
{

struct platform_mutex;
struct platform_condvar;

namespace PlatformCondvar
{

auto API Create(region_alloc &alloc) -> platform_condvar *;
void API Destroy(platform_condvar &self);
void API Wait(platform_condvar &self, platform_mutex &mutex);
void API Signal(platform_condvar &self);

} // namespace PlatformCondvar

} // namespace nyla
