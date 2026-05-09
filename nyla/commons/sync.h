#pragma once

#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc_def.h"

namespace nyla
{

struct mutex;
struct condvar;

namespace Mutex
{

auto API Create(region_alloc &alloc) -> mutex *;
void API Destroy(mutex &self);
void API Lock(mutex &self);
void API Unlock(mutex &self);

} // namespace Mutex

namespace CondVar
{

auto API Create(region_alloc &alloc) -> condvar *;
void API Destroy(condvar &self);
void API Wait(condvar &self, mutex &mutex);
void API Signal(condvar &self);

} // namespace CondVar

} // namespace nyla
