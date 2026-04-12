#include "nyla/commons/macros.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace nyla
{

auto UserMain() -> int;
void NYLA_API NORETURN LibMain(int (*userMain)());

} // namespace nyla

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

    void NORETURN WINAPI _start()
    {
        nyla::LibMain(nyla::UserMain);
        TRAP();
        UNREACHABLE();
    }
}