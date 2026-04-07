#include <cstdint>
#include <cstdarg>

#include "nyla/commons/macros.h"
#include "nyla/commons/span.h"

namespace nyla
{

void NYLA_API StringWriteFmt(char *out, uint64_t outSize, Str fmt, ...);

void NYLA_API FileWriteFmt(FileHandle handle, Str fmt, ...);

inline void NYLA_API FileWriteFmt(FileHandle handle, const char *fmt, uint64_t fmtSize, ...)
{
    va_list args;
    va_start(args, fmtSize);

    FileWriteFmt(handle, AsStr(fmt, fmtSize), args);

    va_end(args);
}

} // namespace nyla