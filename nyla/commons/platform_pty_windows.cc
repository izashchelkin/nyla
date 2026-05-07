#include "nyla/commons/platform_pty.h"

#include <cstdint>

#include "nyla/commons/fmt.h"
#include "nyla/commons/headers_windows.h"
#include "nyla/commons/region_alloc.h"

namespace nyla
{

struct platform_pty
{
    HPCON hpc;
    HANDLE inputWrite;  // parent writes here -> child stdin
    HANDLE outputRead;  // parent reads here  <- child stdout
    HANDLE childInRead; // owned by ConPTY after handoff; we close after CreateProcess
    HANDLE childOutWrite;
    PROCESS_INFORMATION pi;
    LPPROC_THREAD_ATTRIBUTE_LIST attrList;
    bool alive;
};

namespace PlatformPty
{

namespace
{

auto BuildAttrList(region_alloc &alloc, HPCON hpc) -> LPPROC_THREAD_ATTRIBUTE_LIST
{
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    auto *list = (LPPROC_THREAD_ATTRIBUTE_LIST)RegionAlloc::Alloc(alloc, size, alignof(void *));
    if (!InitializeProcThreadAttributeList(list, 1, 0, &size))
        return nullptr;
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hpc, sizeof(HPCON), nullptr, nullptr))
        return nullptr;
    return list;
}

} // namespace

auto API Create(region_alloc &alloc, const platform_pty_spawn_desc &desc) -> platform_pty *
{
    auto &self = RegionAlloc::Alloc<platform_pty>(alloc);
    self.hpc = nullptr;
    self.inputWrite = INVALID_HANDLE_VALUE;
    self.outputRead = INVALID_HANDLE_VALUE;
    self.childInRead = INVALID_HANDLE_VALUE;
    self.childOutWrite = INVALID_HANDLE_VALUE;
    self.attrList = nullptr;
    self.alive = false;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = FALSE;

    if (!CreatePipe(&self.childInRead, &self.inputWrite, &sa, 0))
    {
        LOG("CreatePipe(in) failed: %u"_s, (uint32_t)GetLastError());
        return &self;
    }
    if (!CreatePipe(&self.outputRead, &self.childOutWrite, &sa, 0))
    {
        LOG("CreatePipe(out) failed: %u"_s, (uint32_t)GetLastError());
        return &self;
    }

    COORD size{};
    size.X = (SHORT)desc.cols;
    size.Y = (SHORT)desc.rows;
    HRESULT hr = CreatePseudoConsole(size, self.childInRead, self.childOutWrite, 0, &self.hpc);
    if (FAILED(hr))
    {
        LOG("CreatePseudoConsole failed: 0x%x"_s, (uint32_t)hr);
        return &self;
    }

    self.attrList = BuildAttrList(alloc, self.hpc);
    if (!self.attrList)
    {
        LOG("BuildAttrList failed"_s);
        return &self;
    }

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.lpAttributeList = self.attrList;

    wchar_t cmd[MAX_PATH];
    if (desc.shellPath.size > 0)
    {
        // shellPath assumed UTF-8, null-terminated by caller; convert.
        int n = MultiByteToWideChar(CP_UTF8, 0, (const char *)desc.shellPath.data, (int)desc.shellPath.size, cmd,
                                    MAX_PATH - 1);
        cmd[n] = 0;
    }
    else
    {
        wcscpy_s(cmd, MAX_PATH, L"cmd.exe");
    }

    BOOL ok = CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                             &si.StartupInfo, &self.pi);
    if (!ok)
    {
        LOG("CreateProcessW failed: %u"_s, (uint32_t)GetLastError());
        return &self;
    }

    // ConPTY owns these now.
    CloseHandle(self.childInRead);
    self.childInRead = INVALID_HANDLE_VALUE;
    CloseHandle(self.childOutWrite);
    self.childOutWrite = INVALID_HANDLE_VALUE;

    self.alive = true;
    return &self;
}

void API Destroy(platform_pty &self)
{
    if (self.hpc)
    {
        ClosePseudoConsole(self.hpc);
        self.hpc = nullptr;
    }
    if (self.pi.hProcess)
    {
        WaitForSingleObject(self.pi.hProcess, 1000);
        CloseHandle(self.pi.hProcess);
        CloseHandle(self.pi.hThread);
        self.pi.hProcess = nullptr;
    }
    if (self.inputWrite != INVALID_HANDLE_VALUE)
    {
        CloseHandle(self.inputWrite);
        self.inputWrite = INVALID_HANDLE_VALUE;
    }
    if (self.outputRead != INVALID_HANDLE_VALUE)
    {
        CloseHandle(self.outputRead);
        self.outputRead = INVALID_HANDLE_VALUE;
    }
    if (self.attrList)
    {
        DeleteProcThreadAttributeList(self.attrList);
        self.attrList = nullptr;
    }
    self.alive = false;
}

auto API Read(platform_pty &self, span<uint8_t> out) -> uint32_t
{
    if (self.outputRead == INVALID_HANDLE_VALUE || out.size == 0)
        return 0;

    DWORD avail = 0;
    if (!PeekNamedPipe(self.outputRead, nullptr, 0, nullptr, &avail, nullptr))
        return 0;
    if (avail == 0)
        return 0;

    DWORD toRead = (DWORD)out.size;
    if (avail < toRead)
        toRead = avail;
    DWORD got = 0;
    if (!ReadFile(self.outputRead, out.data, toRead, &got, nullptr))
        return 0;
    return (uint32_t)got;
}

auto API Write(platform_pty &self, byteview bytes) -> uint32_t
{
    if (self.inputWrite == INVALID_HANDLE_VALUE || bytes.size == 0)
        return 0;
    DWORD wrote = 0;
    if (!WriteFile(self.inputWrite, bytes.data, (DWORD)bytes.size, &wrote, nullptr))
        return 0;
    return (uint32_t)wrote;
}

void API Resize(platform_pty &self, uint32_t cols, uint32_t rows)
{
    if (!self.hpc)
        return;
    COORD size{};
    size.X = (SHORT)cols;
    size.Y = (SHORT)rows;
    ResizePseudoConsole(self.hpc, size);
}

auto API IsAlive(platform_pty &self) -> bool
{
    if (!self.alive)
        return false;
    if (!self.pi.hProcess)
        return false;
    DWORD code = 0;
    if (GetExitCodeProcess(self.pi.hProcess, &code) && code != STILL_ACTIVE)
        self.alive = false;
    return self.alive;
}

} // namespace PlatformPty

} // namespace nyla
