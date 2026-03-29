#pragma once

#ifdef _WIN32
#ifdef NYLA_DLL_EXPORT
#define NYLA_API __declspec(dllexport)
#else
#define NYLA_API __declspec(dllimport)
#endif
#else
#define NYLA_API
#endif
