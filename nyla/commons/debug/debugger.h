#pragma once

#if defined(_MSC_VER)
#define DebugBreak() __debugbreak()

#elif defined(__clang__)
#if __has_builtin(__builtin_debugtrap)
#define DebugBreak() __builtin_debugtrap()
#else
#define DebugBreak() __builtin_trap()
#endif

#elif defined(__GNUC__)
#define DebugBreak() __builtin_trap()

#else
#define DebugBreak()                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        *(volatile int *)0 = 0;                                                                                        \
    } while (0)
#endif