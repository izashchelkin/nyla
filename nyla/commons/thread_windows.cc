#include "nyla/commons/thread.h"

#include "nyla/commons/fmt.h"
#include "nyla/commons/headers_windows.h" // IWYU pragma: keep
#include "nyla/commons/region_alloc.h"

namespace nyla
{

struct thread
{
    HANDLE handle;
    void (*fn)(void *userdata);
    void *userdata;
};

namespace Thread
{

namespace
{

auto WINAPI Trampoline(LPVOID arg) -> DWORD
{
    auto *self = static_cast<thread *>(arg);
    self->fn(self->userdata);
    return 0;
}

} // namespace

auto API Create(region_alloc &alloc, void (*fn)(void *userdata), void *userdata) -> thread *
{
    auto &self = RegionAlloc::Alloc<thread>(alloc);
    self.fn = fn;
    self.userdata = userdata;

    self.handle = CreateThread(nullptr, 0, Trampoline, &self, 0, nullptr);
    ASSERT(self.handle);

    return &self;
}

void API Join(thread &self)
{
    DWORD res = WaitForSingleObject(self.handle, INFINITE);
    ASSERT(res == WAIT_OBJECT_0);
    CloseHandle(self.handle);
}

void API SetName(thread &self, const char *name)
{
    wchar_t wide[256];
    int n = MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, 256);
    if (n > 0)
        SetThreadDescription(self.handle, wide);
}

} // namespace Thread

} // namespace nyla