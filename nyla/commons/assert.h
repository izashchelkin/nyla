#pragma once

#include "nyla/commons/fmt.h"

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
        if (!(cond))                                                                                                   \
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