#pragma once

#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc_def.h"

namespace nyla
{

struct thread;

namespace Thread
{

auto API Create(region_alloc &alloc, void (*fn)(void *userdata), void *userdata) -> thread *;
void API Join(thread &self);
void API SetName(thread &self, const char *name);

} // namespace Thread

} // namespace nyla