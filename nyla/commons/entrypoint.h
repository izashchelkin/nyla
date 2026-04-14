#pragma once

namespace nyla
{
void UserMain();
}

#ifdef WIN32
#include "nyla/commons/entrypoint_windows.h" // IWYU pragma: export
#else
#include "nyla/commons/entrypoint_linux.h" // IWYU pragma: export
#endif