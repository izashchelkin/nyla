#pragma once

#include "nyla/commons/memory/region_alloc.h"
#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include <cstdint>

namespace nyla
{

class Engine;

struct CpuAllocs
{
    RegionAlloc permanent;
    RegionAlloc transient;
};

struct EngineInitDesc
{
    uint32_t maxFps;
    bool vsync;

    CpuAllocs &cpuAllocs;
};

struct EngineFrameBeginResult
{
    RhiCmdList cmd;
    float dt;
    uint32_t fps;
};

struct GpuBufferSubAlloc
{
    uint32_t offset;
    uint32_t size;
};

class GpuLinearAllocBuffer
{
  public:
    GpuLinearAllocBuffer(RhiBuffer buffer) : m_Buffer{buffer}
    {
    }

    auto SubAlloc(uint32_t size) -> GpuBufferSubAlloc;

  private:
    RhiBuffer m_Buffer;
    uint64_t m_At{};
};

class Engine
{
  public:
    void Init(const EngineInitDesc &);
    auto ShouldExit() -> bool;

    auto FrameBegin() -> EngineFrameBeginResult;
    auto FrameEnd() -> void;

    auto SubAllocStaticVertexBuffer(uint32_t size) -> GpuBufferSubAlloc;

  private:
    CpuAllocs *m_CpuAllocs{};

    GpuLinearAllocBuffer *m_StaticVertex{};
    GpuLinearAllocBuffer *m_StaticIndex{};

    uint64_t m_TargetFrameDurationUs{};

    uint64_t m_LastFrameStart{};
    uint32_t m_DtUsAccum{};
    uint32_t m_FramesCounted{};
    uint32_t m_Fps{};
    bool m_ShouldExit{};
};
extern Engine *g_Engine;

} // namespace nyla