#pragma once

#include "nyla/rhi/rhi_cmdlist.h"
#include <cstdint>
#include <string_view>

namespace nyla
{

struct DebugTextRenderer;

auto CreateDebugTextRenderer() -> DebugTextRenderer *;
void DebugText(int32_t x, int32_t y, std::string_view text);
void DebugTextRendererDraw(RhiCmdList cmd, DebugTextRenderer *renderer);

} // namespace nyla