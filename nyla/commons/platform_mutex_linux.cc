#include "nyla/commons/platform_mutex.h"

#include "nyla/commons/fmt.h"
#include "nyla/commons/platform_condvar.h"
#include "nyla/commons/region_alloc.h"

#include <pthread.h>

namespace nyla
{

struct platform_mutex
{
    pthread_mutex_t handle;
};

struct platform_condvar
{
    pthread_cond_t handle;
};

namespace PlatformMutex
{

auto API Create(region_alloc &alloc) -> platform_mutex *
{
    auto &self = RegionAlloc::Alloc<platform_mutex>(alloc);

    int res = pthread_mutex_init(&self.handle, nullptr);
    ASSERT(res == 0);

    return &self;
}

void API Destroy(platform_mutex &self)
{
    pthread_mutex_destroy(&self.handle);
}

void API Lock(platform_mutex &self)
{
    pthread_mutex_lock(&self.handle);
}

void API Unlock(platform_mutex &self)
{
    pthread_mutex_unlock(&self.handle);
}

} // namespace PlatformMutex

namespace PlatformCondvar
{

auto API Create(region_alloc &alloc) -> platform_condvar *
{
    auto &self = RegionAlloc::Alloc<platform_condvar>(alloc);

    int res = pthread_cond_init(&self.handle, nullptr);
    ASSERT(res == 0);

    return &self;
}

void API Destroy(platform_condvar &self)
{
    pthread_cond_destroy(&self.handle);
}

void API Wait(platform_condvar &self, platform_mutex &mutex)
{
    pthread_cond_wait(&self.handle, &mutex.handle);
}

void API Signal(platform_condvar &self)
{
    pthread_cond_signal(&self.handle);
}

} // namespace PlatformCondvar

} // namespace nyla
