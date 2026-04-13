#pragma once

#ifdef _MSC_VER

#define INLINE __forceinline
#define RESTRICT __restrict
#define NORETURN __declspec(noreturn)
#define TRAP() __debugbreak()
#define UNREACHABLE() __assume(0)

#ifdef DLL_EXPORT
#define API __declspec(dllexport)
#endif

#ifdef DLL_IMPORT
#define API __declspec(dllimport)
#endif

#else

#define INLINE static inline __attribute__((always_inline))
#define RESTRICT __restrict__
#define NORETURN __attribute__((noreturn))
#define TRAP() __builtin_trap()
#define UNREACHABLE() __builtin_unreachable()

#ifdef DLL_EXPORT
#define API __attribute__((visibility("default")))
#endif

#endif

#ifndef API
#define API
#endif

#define XSTRINGIFY(a) STRINGIFY(a)
#define STRINGIFY(a) #a
