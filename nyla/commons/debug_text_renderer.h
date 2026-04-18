#pragma once

#include <cstdint>

#include "nyla/commons/array_def.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/span.h"

namespace nyla
{

namespace DebugTextRender
{

void API Bootstrap(region_alloc &alloc);

void API Text(int32_t x, int32_t y, byteview text);

template <typename... Args> INLINE void Fmt(int32_t x, int32_t y, byteview fmt, Args... args)
{
    static array<uint8_t, 1024> buffer; // TODO:
    uint64_t writen = StringWriteFmt(buffer, fmt, args...);
    Text(x, y, Span::SubSpan(buffer, 0, writen));
}

void API CmdFlush(rhi_cmdlist cmd);

} // namespace DebugTextRender

} // namespace nyla