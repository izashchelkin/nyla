#define NYLA_ENTRYPOINT

namespace
{

auto PlatformMain() -> int;

}

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
}

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace nyla
{

auto __declspec(dllimport) EntryPointWin32(int (*fnMain)(), HINSTANCE hInstance, HINSTANCE hPrevInstance,
                                           PSTR lpCmdLine, int nCmdShow) -> int;

}

auto WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow) -> int
{
    return nyla::EntryPointWin32(PlatformMain, hInstance, hPrevInstance, lpCmdLine, nCmdShow);
}

#else

#endif