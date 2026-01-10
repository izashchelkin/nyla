#pragma once

#include <cstdio>
#include <cstdlib>

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
            std::fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__, __LINE__, #cond);                          \
            std::fflush(stderr);                                                                                       \
            NYLA_DEBUGBREAK();                                                                                         \
            std::abort();                                                                                              \
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