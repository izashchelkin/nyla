#include "nyla/commons/gpu_upload_manager.h"

#include "nyla/commons/align.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/byteliterals.h"
#include "nyla/commons/log.h"
#include "nyla/commons/rhi.h"
#include <cstdint>

namespace nyla
{

namespace
{

constexpr uint64_t kPerFrameUploadMaxSize = 16_MiB;

RhiBuffer m_StagingBuffer;
uint64_t m_StagingBufferAt;

uint64_t m_StaticVertexBufferSize;
uint64_t m_StaticVertexBufferAt;
RhiBuffer m_StaticVertexBuffer;

uint64_t m_StaticIndexBufferSize;
uint64_t m_StaticIndexBufferAt;
RhiBuffer m_StaticIndexBuffer;

auto PrepareCopySrc(uint64_t copySize) -> uint64_t
{
    AlignUp<uint64_t>(m_StagingBufferAt, Rhi::GetOptimalBufferCopyOffsetAlignment());
    ASSERT(m_StagingBufferAt + copySize <= kPerFrameUploadMaxSize);

    Rhi::BufferMarkWritten(m_StagingBuffer, m_StagingBufferAt, copySize);

    const uint64_t ret = kPerFrameUploadMaxSize * Rhi::GetFrameIndex() + m_StagingBufferAt;
    return ret;
}

} // namespace

void GpuUploadManager::Init()
{
    m_StagingBufferAt = 0;
    m_StagingBuffer = Rhi::CreateBuffer(RhiBufferDesc{
        .size = kPerFrameUploadMaxSize * Rhi::GetNumFramesInFlight(),
        .bufferUsage = RhiBufferUsage::CopySrc,
        .memoryUsage = RhiMemoryUsage::CpuToGpu,
    });
    Rhi::NameBuffer(m_StagingBuffer, AsStr("StagingBuffer"));

    m_StaticVertexBufferSize = 1_GiB;
    m_StaticVertexBufferAt = 0;
    m_StaticVertexBuffer = Rhi::CreateBuffer({
        .size = m_StaticVertexBufferSize,
        .bufferUsage = RhiBufferUsage::Vertex | RhiBufferUsage::CopyDst,
        .memoryUsage = RhiMemoryUsage::GpuOnly,
    });
    Rhi::NameBuffer(m_StaticVertexBuffer, AsStr("StaticVertexBuffer"));

    m_StaticIndexBufferSize = 256_MiB;
    m_StaticIndexBufferAt = 0;
    m_StaticIndexBuffer = Rhi::CreateBuffer({
        .size = m_StaticIndexBufferSize,
        .bufferUsage = RhiBufferUsage::Index | RhiBufferUsage::CopyDst,
        .memoryUsage = RhiMemoryUsage::GpuOnly,
    });
    Rhi::NameBuffer(m_StaticIndexBuffer, AsStr("StaticIndexBuffer"));
}

void GpuUploadManager::FrameBegin()
{
    m_StagingBufferAt = 0;
}

auto GpuUploadManager::CmdCopyBuffer(RhiCmdList cmd, RhiBuffer dst, uint64_t dstOffset, uint64_t copySize) -> char *
{
    uint64_t offset = PrepareCopySrc(copySize);

    Rhi::CmdCopyBuffer(cmd, dst, dstOffset, m_StagingBuffer, offset, copySize);
    m_StagingBufferAt += copySize;

    char *ret = Rhi::MapBuffer(m_StagingBuffer) + offset;
    return ret;
}

auto GpuUploadManager::CmdCopyTexture(RhiCmdList cmd, RhiTexture dst, uint64_t copySize) -> char *
{
    uint64_t offset = PrepareCopySrc(copySize);

    Rhi::CmdCopyTexture(cmd, dst, m_StagingBuffer, offset, copySize);
    m_StagingBufferAt += copySize;

    char *ret = Rhi::MapBuffer(m_StagingBuffer) + offset;
    return ret;
}

auto GpuUploadManager::CmdCopyStaticVertices(RhiCmdList cmd, uint32_t copySize, uint64_t &outBufferOffset) -> char *
{
    outBufferOffset = m_StaticVertexBufferAt;

    char *ret = CmdCopyBuffer(cmd, m_StaticVertexBuffer, m_StaticVertexBufferAt, copySize);
    m_StaticVertexBufferAt += copySize;
    return ret;
}

auto GpuUploadManager::CmdCopyStaticIndices(RhiCmdList cmd, uint32_t copySize, uint64_t &outBufferOffset) -> char *
{
    outBufferOffset = m_StaticIndexBufferAt;

    char *ret = CmdCopyBuffer(cmd, m_StaticIndexBuffer, m_StaticIndexBufferAt, copySize);
    m_StaticIndexBufferAt += copySize;
    return ret;
}

void GpuUploadManager::CmdBindStaticMeshVertexBuffer(RhiCmdList cmd, uint64_t offset)
{
    Rhi::CmdBindVertexBuffers(cmd, 0, {&m_StaticVertexBuffer, 1}, {&offset, 1});
}

void GpuUploadManager::CmdBindStaticMeshIndexBuffer(RhiCmdList cmd, uint64_t offset)
{
    Rhi::CmdBindIndexBuffer(cmd, m_StaticIndexBuffer, offset);
}

} // namespace nyla