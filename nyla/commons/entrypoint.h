#include "nyla/commons/macros.h"

namespace nyla
{

auto PlatformMain() -> int;

} // namespace nyla

#ifdef WIN32

extern "C"
{
    int _fltused = 0;
    void __cdecl _RTC_InitBase()
    {
    }
    void __cdecl _RTC_Shutdown()
    {
    }
    void __cdecl _RTC_CheckStackVars(void *, void *)
    {
    }
    void __chkstk()
    {
    }
}

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace nyla
{

auto NYLA_API EntryPointWin32(int (*fnMain)(), HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine,
                              int nCmdShow) -> int;

}

auto WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow) -> int
{
    return nyla::EntryPointWin32(nyla::PlatformMain, hInstance, hPrevInstance, lpCmdLine, nCmdShow);
}

#else

#endif