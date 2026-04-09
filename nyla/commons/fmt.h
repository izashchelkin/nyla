#pragma once

#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h" // IWYU pragma: export
#include "nyla/commons/platform_base.h"

namespace nyla
{

void NYLA_API FileWriteFmt(FileHandle handle, const char *fmt, uint64_t fmtSize, ...);

} // namespace nyla

#define NYLA_LOG(fmt, ...) ::nyla::FileWriteFmt(Platform::GetStderr(), fmt, CStrLen(fmt), ##__VA_ARGS__)

#define NYLA_SV_FMT "%.*s"
#define NYLA_SV_ARG(sv) (uint32_t)((sv).Size()), (sv).Data()

#if defined(_MSC_VER)
#define NYLA_DEBUGBREAK() __debugbreak()
#elif defined(__clang__)
#if __has_builtin(__builtin_debugtrap)
#define NYLA_DEBUGBREAK() __builtin_debugtrap()
#else
#define NYLA_DEBUGBREAK() __builtin_trap()
#endif
#elif defined(__GNUC__)
#define NYLA_DEBUGBREAK() __builtin_trap()
#else
#define NYLA_DEBUGBREAK()                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        *(volatile int *)0 = 0;                                                                                        \
    } while (0)
#endif

#define NYLA_ASSERT(cond)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond)) [[unlikely]]                                                                                      \
        {                                                                                                              \
            NYLA_LOG("%s:%d: assertion failed: %s", __FILE__, __LINE__, #cond);                                        \
            NYLA_DEBUGBREAK();                                                                                         \
            *(volatile int *)(0) = 0;                                                                                  \
        }                                                                                                              \
    } while (0)

#ifdef NDEBUG
#define NYLA_DASSERT(cond)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        (void)sizeof(cond);                                                                                            \
    } while (0)
#else
#define NYLA_DASSERT(cond) NYLA_ASSERT(cond)
#endif