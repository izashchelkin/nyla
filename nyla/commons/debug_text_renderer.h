#pragma once

#include <cstdarg>
#include <cstdint>

#include "nyla/commons/array_def.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/span.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

namespace DebugTextRenderer
{

void API Bootstrap(region_alloc &alloc);

void API Text(int32_t x, int32_t y, byteview text);

INLINE void Fmt(int32_t x, int32_t y, byteview fmt, ...)
{
    static array<uint8_t, 1024> buffer; // TODO:

    va_list args;
    va_start(args, fmt);
    uint64_t writen = StringWriteFmt(buffer, fmt, args);
    va_end(args);

    Text(x, y, Span::SubSpan((byteview)buffer, 0, writen));
}

void API CmdFlush(rhi_cmdlist cmd);

} // namespace DebugTextRenderer

} // namespace nyla