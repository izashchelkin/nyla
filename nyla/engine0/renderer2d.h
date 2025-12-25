#pragma once

#include "nyla/commons/math/vec.h"
#include "nyla/engine0/gpu_resources.h"
#include "nyla/engine0/staging_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include <cstdint>

namespace nyla
{

struct Renderer2D;

auto CreateRenderer2D(AssetManager *assetManager) -> Renderer2D *;
void Renderer2DFrameBegin(RhiCmdList cmd, Renderer2D *renderer, GpuStagingBuffer *stagingBuffer);
void Renderer2DRect(RhiCmdList cmd, Renderer2D *renderer, float x, float y, float width, float height, float4 color,
                    uint32_t textureIndex);
void Renderer2DDraw(RhiCmdList cmd, Renderer2D *renderer, uint32_t width, uint32_t height, float metersOnScreen);

} // namespace nyla