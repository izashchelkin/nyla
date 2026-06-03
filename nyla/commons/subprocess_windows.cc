#include "nyla/commons/subprocess.h"

namespace nyla
{

auto API SubprocessRun(span<const char *const> cmd, region_alloc &alloc, byteview stdin_data, uint32_t timeout_ms)
    -> subprocess_result
{
    (void)cmd;
    (void)alloc;
    (void)stdin_data;
    (void)timeout_ms;

    subprocess_result result = {};
    result.exit_code = -1;
    result.timed_out = false;
    return result;
}

} // namespace nyla
