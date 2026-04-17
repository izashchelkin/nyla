#pragma once

#define LOG(fmt, ...) FileWriteFmt(GetStderr(), fmt ""_s, ##__VA_ARGS__)

#define SV_FMT "%.*s"
#define SV_ARG(sv) (sv).size, (sv).data

#define ASSERT(cond, ...)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond)) [[unlikely]]                                                                                      \
        {                                                                                                              \
            LOG(__FILE__ ":" XSTRINGIFY(__LINE__) ": assertion failed: " #cond __VA_OPT__(" | ") __VA_ARGS__);         \
            TRAP();                                                                                                    \
            UNREACHABLE();                                                                                             \
        }                                                                                                              \
    } while (0)

#ifdef NDEBUG
#define DASSERT(cond)                                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        (void)sizeof(cond);                                                                                            \
    } while (0)
#else
#define DASSERT(cond) ASSERT(cond)
#endif

#include "nyla/commons/file.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

void API FileWriteFmt(file_handle handle, span<uint8_t> buffer, byteview fmt, ...);
void API FileWriteFmt(file_handle handle, byteview fmt, ...);
auto API StringWriteFmt(span<uint8_t> out, byteview fmt, ...) -> uint64_t;

} // namespace nyla