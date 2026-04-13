#pragma once

#include <cstdint>

#include "nyla/commons/rhi.h"

namespace nyla
{

struct debug_text_renderer
{
};

namespace DebugTextRender
{
}

class DebugTextRenderer
{
  public:
    static void Init();
    static void Text(int32_t x, int32_t y, Str text);
    static void Fmt(int32_t x, int32_t y, const char *format, auto... valist)
    {
        auto buf = Engine::GetPerFrameAlloc().PushArr<char>(256);
        snprintf(buf.Data(), buf.Size(), format, valist...);
        Text(x, y, Str{buf.Data(), buf.Size()});
    }
    static void CmdFlush(RhiCmdList cmd);
};

} // namespace nyla