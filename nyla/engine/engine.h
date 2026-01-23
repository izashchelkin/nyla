#pragma once

#include "nyla/commons/memory/region_alloc.h"
#include "nyla/engine/asset_manager.h"
#include "nyla/engine/staging_buffer.h"
#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include <cstdint>

namespace nyla
{

class Engine;

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
    void Init(RhiBuffer buffer);
    void Reset();
    auto SubAlloc(uint32_t size, uint32_t align) -> GpuBufferSubAlloc;

    auto GetBuffer() -> RhiBuffer
    {
        return m_Buffer;
    }

  private:
    RhiBuffer m_Buffer;
    uint64_t m_At;
};

struct CpuAllocs
{
    RegionAlloc permanent;
    RegionAlloc transient;
};

class Engine
{
  public:
    void Init(const EngineInitDesc &);
    auto ShouldExit() -> bool;

    auto FrameBegin() -> EngineFrameBeginResult;
    auto FrameEnd() -> void;

    auto SubAllocStaticVertexBuffer(uint32_t size) -> GpuBufferSubAlloc;

    auto UploadBuffer(RhiCmdList cmd, RhiBuffer dst, uint32_t dstOffset, uint32_t size) -> char *;
    auto UploadTexture(RhiCmdList cmd, RhiTexture dst, uint32_t size) -> char *;

    auto GetCpuAllocs() -> CpuAllocs &
    {
        return m_CpuAllocs;
    }

    auto GetAssetManager() -> AssetManager &
    {
        return m_AssetManager;
    }

  private:
    CpuAllocs m_CpuAllocs;
    AssetManager m_AssetManager;

    GpuLinearAllocBuffer m_UploadBuffer;
    GpuLinearAllocBuffer m_StaticVertex;
    GpuLinearAllocBuffer m_StaticIndex;

    uint64_t m_TargetFrameDurationUs{};

    uint64_t m_LastFrameStart{};
    uint32_t m_DtUsAccum{};
    uint32_t m_FramesCounted{};
    uint32_t m_Fps{};
    bool m_ShouldExit{};
};
extern Engine *g_Engine;

} // namespace nyla