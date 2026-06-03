#pragma once

#include "nyla/commons/inline_vec.h"
#include "nyla/commons/span.h"

struct region_alloc;
struct timespec;

namespace nyla
{

struct cli_args
{
    inline_vec<byteview, 16> positional;
    byteview command;
};

void LogUsage(region_alloc *alloc, const cli_args &args, struct timespec t_start, int exit_code, const char *error_tag);

} // namespace nyla
