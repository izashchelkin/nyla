#include "nyla/commons/platform_mutex.h"

#include "nyla/commons/fmt.h"
#include "nyla/commons/headers_windows.h"
#include "nyla/commons/platform_condvar.h"
#include "nyla/commons/region_alloc.h"

namespace nyla
{

struct platform_mutex
{
    SRWLOCK handle;
};

struct platform_condvar
{
    CONDITION_VARIABLE handle;
};

namespace PlatformMutex
{

auto API Create(region_alloc &alloc) -> platform_mutex *
{
    auto &self = RegionAlloc::Alloc<platform_mutex>(alloc);

    InitializeSRWLock(&self.handle);

    return &self;
}

void API Destroy(platform_mutex &self)
{
}

void API Lock(platform_mutex &self)
{
    AcquireSRWLockExclusive(&self.handle);
}

void API Unlock(platform_mutex &self)
{
    ReleaseSRWLockExclusive(&self.handle);
}

} // namespace PlatformMutex

namespace PlatformCondvar
{

auto API Create(region_alloc &alloc) -> platform_condvar *
{
    auto &self = RegionAlloc::Alloc<platform_condvar>(alloc);

    InitializeConditionVariable(&self.handle);

    return &self;
}

void API Destroy(platform_condvar &self)
{
}

void API Wait(platform_condvar &self, platform_mutex &mutex)
{
    SleepConditionVariableSRW(&self.handle, &mutex.handle, INFINITE, 0);
}

void API Signal(platform_condvar &self)
{
    WakeConditionVariable(&self.handle);
}

} // namespace PlatformCondvar

} // namespace nyla
