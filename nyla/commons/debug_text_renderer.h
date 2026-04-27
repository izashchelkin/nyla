#pragma once

#include <cstdarg>
#include <cstdint>

#include "nyla/commons/fmt.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

namespace DebugTextRenderer
{

void API Bootstrap(region_alloc &alloc);

void API Text(int32_t x, int32_t y, byteview text);

INLINE void Fmt(int32_t x, int32_t y, byteview fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    byteview formatted = StringWriteFmt_(fmt, args);
    va_end(args);

    Text(x, y, formatted);
}

void API CmdFlush(rhi_cmdlist cmd);

} // namespace DebugTextRenderer

} // namespace nyla