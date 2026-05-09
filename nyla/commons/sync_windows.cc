#include "nyla/commons/sync.h"

#include "nyla/commons/headers_windows.h" // IWYU pragma: keep
#include "nyla/commons/region_alloc.h"

namespace nyla
{

struct mutex
{
    SRWLOCK handle;
};

struct condvar
{
    CONDITION_VARIABLE handle;
};

namespace Mutex
{

auto API Create(region_alloc &alloc) -> mutex *
{
    auto &self = RegionAlloc::Alloc<mutex>(alloc);
    InitializeSRWLock(&self.handle);
    return &self;
}

void API Destroy(mutex &self)
{
}

void API Lock(mutex &self)
{
    AcquireSRWLockExclusive(&self.handle);
}

void API Unlock(mutex &self)
{
    ReleaseSRWLockExclusive(&self.handle);
}

} // namespace Mutex

namespace CondVar
{

auto API Create(region_alloc &alloc) -> condvar *
{
    auto &self = RegionAlloc::Alloc<condvar>(alloc);
    InitializeConditionVariable(&self.handle);
    return &self;
}

void API Destroy(condvar &self)
{
}

void API Wait(condvar &self, mutex &mutex)
{
    SleepConditionVariableSRW(&self.handle, &mutex.handle, INFINITE, 0);
}

void API Signal(condvar &self)
{
    WakeConditionVariable(&self.handle);
}

} // namespace CondVar

} // namespace nyla