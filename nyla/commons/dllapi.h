#pragma once

#ifdef _WIN32

#ifdef NYLA_DLL_EXPORT
#define NYLA_API __declspec(dllexport)
#endif

#ifdef NYLA_DLL_IMPORT
#define NYLA_API __declspec(dllimport)
#endif

#else
#endif

#ifndef NYLA_API
#define NYLA_API
#endif