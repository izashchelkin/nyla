#pragma once

#include <cstdint>

#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/span.h"

namespace nyla
{

struct subprocess_result
{
    byteview stdout_data; // region-allocated, may be empty
    byteview stderr_data; // region-allocated, may be empty
    int32_t exit_code;    // -1 on failure to launch
    bool timed_out;       // true if killed by timeout
};

// Run a subprocess, capturing stdout and stderr separately.
// cmd: null-terminated array of arguments (cmd[0] = executable, cmd[last] = nullptr)
// alloc: region allocator for output buffers (stdout_data, stderr_data)
// stdin_data: optional input piped to subprocess stdin (empty = /dev/null)
// timeout_ms: max runtime in milliseconds (0 = no timeout)
auto API SubprocessRun(span<const char *const> cmd, region_alloc &alloc, byteview stdin_data = {},
                       uint32_t timeout_ms = 0) -> subprocess_result;

} // namespace nyla
