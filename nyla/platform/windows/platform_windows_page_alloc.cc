#include "nyla/platform/windows/platform_windows.h"
#include <cstdint>

namespace nyla
{

auto Platform::Impl::PageAlloc(uint32_t &inOutSize, void *&outBase) -> bool
{
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    inOutSize = (inOutSize / sysInfo.dwPageSize);
    if (inOutSize % sysInfo.dwPageSize)
        inOutSize += sysInfo.dwPageSize;

    outBase = VirtualAlloc(nullptr, inOutSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    return outBase != nullptr;
}

auto Platform::PageAlloc(uint32_t &inOutSize, void *&outBase) -> bool
{
    return m_Impl->PageAlloc(inOutSize, outBase);
}

} // namespace nyla