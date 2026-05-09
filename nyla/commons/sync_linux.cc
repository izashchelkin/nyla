#include "nyla/commons/sync.h"

#include "nyla/commons/fmt.h"
#include "nyla/commons/region_alloc.h"

#include <pthread.h>

namespace nyla
{

struct mutex
{
    pthread_mutex_t handle;
};

struct condvar
{
    pthread_cond_t handle;
};

namespace Mutex
{

auto API Create(region_alloc &alloc) -> mutex *
{
    auto &self = RegionAlloc::Alloc<mutex>(alloc);

    int res = pthread_mutex_init(&self.handle, nullptr);
    ASSERT(res == 0);

    return &self;
}

void API Destroy(mutex &self)
{
    pthread_mutex_destroy(&self.handle);
}

void API Lock(mutex &self)
{
    pthread_mutex_lock(&self.handle);
}

void API Unlock(mutex &self)
{
    pthread_mutex_unlock(&self.handle);
}

} // namespace Mutex

namespace CondVar
{

auto API Create(region_alloc &alloc) -> condvar *
{
    auto &self = RegionAlloc::Alloc<condvar>(alloc);

    int res = pthread_cond_init(&self.handle, nullptr);
    ASSERT(res == 0);

    return &self;
}

void API Destroy(condvar &self)
{
    pthread_cond_destroy(&self.handle);
}

void API Wait(condvar &self, mutex &mutex)
{
    pthread_cond_wait(&self.handle, &mutex.handle);
}

void API Signal(condvar &self)
{
    pthread_cond_signal(&self.handle);
}

} // namespace CondVar

} // namespace nyla