#pragma once

#include "nyla/commons/math/vec.h"
#include "nyla/engine0/staging_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"

namespace nyla
{

struct Renderer2D;

auto CreateRenderer2D() -> Renderer2D *;
void Renderer2DFrameBegin(RhiCmdList cmd, Renderer2D *renderer, StagingBuffer *stagingBuffer);
void Renderer2DPassBegin(RhiCmdList cmd, Renderer2D *renderer, uint32_t width, uint32_t height, float metersOnScreen);
void Renderer2DDrawRect(RhiCmdList cmd, Renderer2D *renderer, float x, float y, float width, float height,
                        float4 color);

} // namespace nyla