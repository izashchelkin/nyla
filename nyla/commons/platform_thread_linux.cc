#include "nyla/commons/platform_thread.h"

#include "nyla/commons/fmt.h"
#include "nyla/commons/region_alloc.h"

#include <pthread.h>

namespace nyla
{

struct platform_thread
{
    pthread_t handle;
    platform_thread_fn fn;
    void *userdata;
};

namespace PlatformThread
{

namespace
{

auto Trampoline(void *arg) -> void *
{
    auto *self = static_cast<platform_thread *>(arg);
    self->fn(self->userdata);
    return nullptr;
}

} // namespace

auto API Create(region_alloc &alloc, platform_thread_fn fn, void *userdata) -> platform_thread *
{
    auto &self = RegionAlloc::Alloc<platform_thread>(alloc);
    self.fn = fn;
    self.userdata = userdata;

    int res = pthread_create(&self.handle, nullptr, &Trampoline, &self);
    ASSERT(res == 0);

    return &self;
}

void API Join(platform_thread &self)
{
    int res = pthread_join(self.handle, nullptr);
    ASSERT(res == 0);
}

void API SetName(platform_thread &self, const char *name)
{
    pthread_setname_np(self.handle, name);
}

} // namespace PlatformThread

} // namespace nyla
