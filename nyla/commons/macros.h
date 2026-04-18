#pragma once

#if (defined(__clang__) || defined(__GNUC__)) && !defined(_MSC_VER)

#define INLINE static inline __attribute__((always_inline))
#define RESTRICT __restrict__
#define NORETURN __attribute__((noreturn))
#define TRAP() __builtin_trap()
#define UNREACHABLE() __builtin_unreachable()
#define ASSUME(cond) [[assume(cond)]]

#ifdef DLL_EXPORT
#define API __attribute__((visibility("default")))
#endif

#else

#define INLINE __forceinline
#define RESTRICT __restrict
#define NORETURN __declspec(noreturn)
#define TRAP() __debugbreak()
#define UNREACHABLE() __assume(0)
#define ASSUME(cond) __assume(cond)

#ifdef DLL_EXPORT
#define API __declspec(dllexport)
#endif

#ifdef DLL_IMPORT
#define API __declspec(dllimport)
#endif

#endif

#ifndef API
#define API
#endif

#define XSTRINGIFY(a) STRINGIFY(a)
#define STRINGIFY(a) #a