#pragma once

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
};

struct EngineFrameBeginResult
{
    RhiCmdList cmd;
    float dt;
    uint32_t fps;
};

class Engine
{
  public:
    void Init(const EngineInitDesc &);
    auto ShouldExit() -> bool;

    auto FrameBegin() -> EngineFrameBeginResult;
    auto FrameEnd() -> void;

    auto AllocStaticMeshBuffer(uint32_t size) -> ;

  private:
    RhiBuffer m_BufferStaticMesh;
    uint32_t m_BufferStaticMeshSize{};
    uint32_t m_BufferStaticMeshAt{};

    uint64_t m_TargetFrameDurationUs{};

    uint64_t m_LastFrameStart{};
    uint32_t m_DtUsAccum{};
    uint32_t m_FramesCounted{};
    uint32_t m_Fps{};
    bool m_ShouldExit{};
};
extern Engine *g_Engine;

} // namespace nyla