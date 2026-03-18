#pragma once

#include "nyla/engine/engine.h"
#include "nyla/rhi/rhi.h"
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace nyla
{

class DebugTextRenderer
{
  public:
    static void Init();
    static void Text(int32_t x, int32_t y, std::string_view text);
    static void Fmt(int32_t x, int32_t y, const char *format, auto... valist)
    {
        auto buf = Engine::GetPerFrameAlloc().PushArr<char>(256);
        snprintf(buf.data(), buf.size(), format, valist...);
        Text(x, y, std::string_view{buf.data(), buf.size()});
    }
    static void CmdFlush(RhiCmdList cmd);
};

} // namespace nyla