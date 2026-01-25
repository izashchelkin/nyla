#include "nyla/engine/gpu_upload_manager.h"
#include "nyla/commons/align.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_buffer.h"
#include <cstdint>

namespace nyla
{

constexpr uint64_t kPerFrameSize = 16_MiB;

void GpuUploadManager::Init()
{
    m_StagingBufferWritten = 0;
    m_StagingBuffer = g_Rhi.CreateBuffer(RhiBufferDesc{
        .size = kPerFrameSize * g_Rhi.GetNumFramesInFlight(),
        .bufferUsage = RhiBufferUsage::CopySrc,
        .memoryUsage = RhiMemoryUsage::CpuToGpu,
    });
}

void GpuUploadManager::FrameBegin()
{
    m_StagingBufferWritten = 0;
}

auto GpuUploadManager::PrepareCopySrc(uint64_t copySize) -> uint64_t
{
    AlignUp<uint64_t>(m_StagingBufferWritten, g_Rhi.GetOptimalBufferCopyOffsetAlignment());
    NYLA_ASSERT(m_StagingBufferWritten + copySize <= kPerFrameSize);

    g_Rhi.BufferMarkWritten(m_StagingBuffer, m_StagingBufferWritten, copySize);

    const uint64_t ret = kPerFrameSize * g_Rhi.GetFrameIndex() + m_StagingBufferWritten;
    return ret;
}

auto GpuUploadManager::CmdCopyBuffer(RhiCmdList cmd, RhiBuffer dst, uint32_t dstOffset, uint32_t copySize) -> char *
{
    uint64_t offset = PrepareCopySrc(copySize);

    g_Rhi.CmdCopyBuffer(cmd, dst, dstOffset, m_StagingBuffer, offset, copySize);
    m_StagingBufferWritten += copySize;

    char *ret = g_Rhi.MapBuffer(m_StagingBuffer) + offset;
    return ret;
}

auto GpuUploadManager::CmdCopyTexture(RhiCmdList cmd, RhiTexture dst, uint32_t copySize) -> char *
{
    uint64_t offset = PrepareCopySrc(copySize);

    g_Rhi.CmdCopyTexture(cmd, dst, m_StagingBuffer, offset, copySize);
    m_StagingBufferWritten += copySize;

    char *ret = g_Rhi.MapBuffer(m_StagingBuffer) + offset;
    return ret;
}

} // namespace nyla