#include "nyla/commons/thread.h"

#include "nyla/commons/fmt.h"
#include "nyla/commons/region_alloc.h"

#include <pthread.h>

namespace nyla
{

struct thread
{
    pthread_t handle;
    void (*fn)(void *userdata);
    void *userdata;
};

namespace Thread
{

namespace
{

auto Trampoline(void *arg) -> void *
{
    auto *self = static_cast<thread *>(arg);
    self->fn(self->userdata);
    return nullptr;
}

} // namespace

auto API Create(region_alloc &alloc, void (*fn)(void *userdata), void *userdata) -> thread *
{
    auto &self = RegionAlloc::Alloc<thread>(alloc);
    self.fn = fn;
    self.userdata = userdata;

    int res = pthread_create(&self.handle, nullptr, &Trampoline, &self);
    ASSERT(res == 0);

    return &self;
}

void API Join(thread &self)
{
    int res = pthread_join(self.handle, nullptr);
    ASSERT(res == 0);
}

void API SetName(thread &self, const char *name)
{
    pthread_setname_np(self.handle, name);
}

} // namespace Thread

} // namespace nyla
