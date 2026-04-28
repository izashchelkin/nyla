#include "nyla/commons/platform_thread.h"

#include "nyla/commons/fmt.h"
#include "nyla/commons/headers_windows.h"
#include "nyla/commons/region_alloc.h"

namespace nyla
{

struct platform_thread
{
    HANDLE handle;
    platform_thread_fn fn;
    void *userdata;
};

namespace PlatformThread
{

namespace
{

auto WINAPI Trampoline(LPVOID arg) -> DWORD
{
    auto *self = static_cast<platform_thread *>(arg);
    self->fn(self->userdata);
    return 0;
}

} // namespace

auto API Create(region_alloc &alloc, platform_thread_fn fn, void *userdata) -> platform_thread *
{
    auto &self = RegionAlloc::Alloc<platform_thread>(alloc);
    self.fn = fn;
    self.userdata = userdata;

    self.handle = CreateThread(nullptr, 0, &Trampoline, &self, 0, nullptr);
    ASSERT(self.handle);

    return &self;
}

void API Join(platform_thread &self)
{
    DWORD res = WaitForSingleObject(self.handle, INFINITE);
    ASSERT(res == WAIT_OBJECT_0);
    CloseHandle(self.handle);
}

void API SetName(platform_thread &self, const char *name)
{
    wchar_t wide[256];
    int n = MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, 256);
    if (n > 0)
        SetThreadDescription(self.handle, wide);
}

} // namespace PlatformThread

} // namespace nyla
