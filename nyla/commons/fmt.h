#pragma once

#include <cstdarg>

namespace nyla
{

using FileHandle = void*;

void FmtWrite(FileHandle handle, const char *fmt, va_list args);
void LogWrapper(const char *fmt, ...);

} // namespace nyla

#define NYLA_LOG(fmt, ...) ::nyla::LogWrapper(fmt, ##__VA_ARGS__)

#define NYLA_SV_FMT "%.*s"
#define NYLA_SV_ARG(sv) (int)(sv).size(), (sv).data()