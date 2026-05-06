#pragma once

#include "nyla/commons/entrypoint.h"
#include "nyla/commons/libmain.h"

auto main() -> int
{
    nyla::LibMain(nyla::UserMain);
    return 0;
}
