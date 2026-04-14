#pragma once

#include "nyla/commons/entrypoint.h"
#include "nyla/commons/headers_windows.h"
#include "nyla/commons/libmain.h"

int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    nyla::LibMain(nyla::UserMain);
    return 0;
}