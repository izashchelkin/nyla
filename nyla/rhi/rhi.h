#pragma once

#include <cstdint>

#include "nyla/commons/bitenum.h"
#include "nyla/platform/platform.h"

#include "rhi.h"
#include "rhi_buffer.h"
#include "rhi_cmdlist.h"
#include "rhi_descriptor.h"
#include "rhi_pass.h"
#include "rhi_pipeline.h"
#include "rhi_sampler.h"
#include "rhi_shader.h"
#include "rhi_texture.h"

namespace nyla
{

constexpr inline uint32_t kRhiMaxNumFramesInFlight = 3;
constexpr inline uint32_t kRhiMaxNumSwapchainTextures = 4;

#if defined(NDEBUG)
constexpr inline bool kRhiValidations = false;
#else
constexpr inline bool kRhiValidations = true;
#endif

constexpr inline bool kRhiCheckpoints = false;

//

enum class RhiFlags : uint32_t
{
    VSync = 1 << 0,
};
NYLA_BITENUM(RhiFlags);

struct RhiLimits
{
    uint32_t numTextures = 64;
    uint32_t numTextureViews = 64;

    uint32_t numBuffers = 16;
    uint32_t numCBVs = 16;

    uint32_t numSamplers = 8;

    uint32_t numFramesInFlight = 2;
    uint32_t maxDrawCount = 1024;
    uint32_t maxPassCount = 4;

    uint32_t perFrameConstantSize = 256;
    uint32_t perPassConstantSize = 512;
    uint32_t perDrawConstantSize = 256;
    uint32_t perDrawLargeConstantSize = 1024;
};

struct RhiInitDesc
{
    PlatformWindow window;
    RhiFlags flags;
    RhiLimits limits;
};

class Rhi
{
  public:
    void Init(const RhiInitDesc &);
    auto GetNumFramesInFlight() -> uint32_t;
    auto GetFrameIndex() -> uint32_t;
    auto GetMinUniformBufferOffsetAlignment() -> uint32_t;
    auto GetOptimalBufferCopyOffsetAlignment() -> uint32_t;

    auto CreateBuffer(const RhiBufferDesc &) -> RhiBuffer;
    void NameBuffer(RhiBuffer, std::string_view name);
    void DestroyBuffer(RhiBuffer);

    auto GetBufferSize(RhiBuffer) -> uint32_t;

    auto MapBuffer(RhiBuffer) -> char *;
    void UnmapBuffer(RhiBuffer);
    void BufferMarkWritten(RhiBuffer, uint32_t offset, uint32_t size);

    void CmdCopyBuffer(RhiCmdList cmd, RhiBuffer dst, uint32_t dstOffset, RhiBuffer src, uint32_t srcOffset,
                       uint32_t size);
    void CmdTransitionBuffer(RhiCmdList cmd, RhiBuffer buffer, RhiBufferState newState);
    void CmdUavBarrierBuffer(RhiCmdList cmd, RhiBuffer buffer);

    auto CreateCmdList(RhiQueueType queueType) -> RhiCmdList;
    void NameCmdList(RhiCmdList, std::string_view name);
    void DestroyCmdList(RhiCmdList cmd);

    auto CmdSetCheckpoint(RhiCmdList cmd, uint64_t data) -> uint64_t;
    auto GetLastCheckpointData(RhiQueueType queueType) -> uint64_t;

    auto FrameBegin() -> RhiCmdList;
    void FrameEnd();

    auto FrameGetCmdList() -> RhiCmdList;

    auto CreateDescriptorSetLayout(const RhiDescriptorSetLayoutDesc &) -> RhiDescriptorSetLayout;
    void DestroyDescriptorSetLayout(RhiDescriptorSetLayout);

    auto CreateDescriptorSet(RhiDescriptorSetLayout) -> RhiDescriptorSet;
    void DestroyDescriptorSet(RhiDescriptorSet);
    void WriteDescriptors(std::span<const RhiDescriptorWriteDesc>);

    void PassBegin(RhiPassDesc);
    void PassEnd(RhiPassDesc);

    auto GetVertexFormatSize(RhiVertexFormat) -> uint32_t;

    auto CreateGraphicsPipeline(const RhiGraphicsPipelineDesc &) -> RhiGraphicsPipeline;
    void NameGraphicsPipeline(RhiGraphicsPipeline, std::string_view name);
    void DestroyGraphicsPipeline(RhiGraphicsPipeline);

    void CmdBindGraphicsPipeline(RhiCmdList, RhiGraphicsPipeline);
    void CmdBindVertexBuffers(RhiCmdList cmd, uint32_t firstBinding, std::span<const RhiBuffer> buffers,
                              std::span<const uint32_t> offsets);
    void CmdBindGraphicsBindGroup(RhiCmdList, uint32_t setIndex, RhiDescriptorSet bindGroup,
                                  std::span<const uint32_t> dynamicOffsets);
    void CmdPushGraphicsConstants(RhiCmdList cmd, uint32_t offset, RhiShaderStage stage, ByteView data);
    void CmdDraw(RhiCmdList cmd, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
                 uint32_t firstInstance);

    auto CreateSampler(const RhiSamplerDesc &) -> RhiSampler;
    void DestroySampler(RhiSampler);

    auto CreateShader(const RhiShaderDesc &) -> RhiShader;
    void DestroyShader(RhiShader);

    auto CreateTexture(const RhiTextureDesc &) -> RhiTexture;
    void DestroyTexture(RhiTexture);
    auto GetTextureInfo(RhiTexture) -> RhiTextureInfo;
    void CmdTransitionTexture(RhiCmdList, RhiTexture, RhiTextureState);
    void CmdCopyTexture(RhiCmdList cmd, RhiTexture dst, RhiBuffer src, uint32_t srcOffset, uint32_t size);

    auto CreateTextureView(const RhiTextureViewDesc &) -> RhiTextureView;
    void DestroyTextureView(RhiTextureView);

    auto GetBackbufferTexture() -> RhiTexture;

  private:
    class Impl;
    Impl *m_Impl;
};
extern Rhi *g_Rhi;

} // namespace nyla