#pragma once

#include "nyla/commons/bitenum.h"
#include "nyla/rhi/rhi_handle.h"

namespace nyla {

enum class RhiBufferUsage : uint32_t {
  Vertex = 1 << 0,
  Index = 1 << 1,
  Uniform = 1 << 2,
  TransferSrc = 1 << 3,
  TransferDst = 1 << 4,
};

template <>
struct EnableBitMaskOps<RhiBufferUsage> : std::true_type {};

enum class RhiMemoryUsage { GpuOnly, CpuToGpu, GpuToCpu };

struct RhiBuffer : RhiHandle {};

struct RhiBufferDesc {
  uint32_t size;
  RhiBufferUsage buffer_usage;
  RhiMemoryUsage memory_usage;
};

RhiBuffer RhiCreateBuffer(const RhiBufferDesc&);
void RhiNameBuffer(RhiBuffer, std::string_view name);
void RhiDestroyBuffer(RhiBuffer);

void* RhiMapBuffer(RhiBuffer, bool idempotent = true);
void RhiUnmapBuffer(RhiBuffer, bool idempotent = true);

}  // namespace nyla