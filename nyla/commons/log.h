#pragma once

#include <cstdio>
#include <cstring>

#if defined(_MSC_VER)
#define NYLA_LOG_FILE() (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)
#else
#define NYLA_LOG_FILE() (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

#define NYLA_LOG(fmt, ...)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        std::fprintf(stderr, "%s:%d: " fmt "\n", NYLA_LOG_FILE(), __LINE__, ##__VA_ARGS__);                            \
        std::fflush(stderr);                                                                                           \
    } while (0)

#define NYLA_SV_FMT "%.*s"
#define NYLA_SV_ARG(sv) (int)(sv).size(), (sv).data()