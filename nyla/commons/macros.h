#pragma once

#ifdef _MSC_VER

#define INLINE __forceinline
#define RESTRICT __restrict
#define NORETURN __declspec(noreturn)
#define TRAP() __debugbreak()
#define UNREACHABLE() __assume(0)

#ifdef NYLA_DLL_EXPORT
#define NYLA_API __declspec(dllexport)
#endif

#ifdef NYLA_DLL_IMPORT
#define NYLA_API __declspec(dllimport)
#endif

#else

#define INLINE static inline __attribute__((always_inline))
#define RESTRICT __restrict__
#define NORETURN __attribute__((noreturn))
#define TRAP() __builtin_trap()
#define UNREACHABLE() __builtin_unreachable()

#ifdef NYLA_DLL_EXPORT
#define NYLA_API __attribute__((visibility("default")))
#endif

#endif

#ifndef NYLA_API
#define NYLA_API
#endif