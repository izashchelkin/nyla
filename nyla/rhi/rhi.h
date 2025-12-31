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

struct RhiInitDesc
{
    RhiFlags flags;

    PlatformWindow window;
    uint32_t numFramesInFlight;
};

class Rhi
{
  public:
    void Init(const RhiInitDesc &);
    auto GetNumFramesInFlight() -> uint32_t;
    auto GetFrameIndex() -> uint32_t;
    auto GetMinUniformBufferOffsetAlignment() -> uint32_t;
    auto GetOptimalBufferCopyOffsetAlignment() -> uint32_t;

    auto RhiCreateBuffer(const RhiBufferDesc &) -> RhiBuffer;
    void RhiNameBuffer(RhiBuffer, std::string_view name);
    void RhiDestroyBuffer(RhiBuffer);

    auto RhiGetBufferSize(RhiBuffer) -> uint32_t;

    auto RhiMapBuffer(RhiBuffer) -> char *;
    void RhiUnmapBuffer(RhiBuffer);
    void RhiBufferMarkWritten(RhiBuffer, uint32_t offset, uint32_t size);

    void RhiCmdCopyBuffer(RhiCmdList cmd, RhiBuffer dst, uint32_t dstOffset, RhiBuffer src, uint32_t srcOffset,
                          uint32_t size);
    void RhiCmdTransitionBuffer(RhiCmdList cmd, RhiBuffer buffer, RhiBufferState newState);
    void RhiCmdUavBarrierBuffer(RhiCmdList cmd, RhiBuffer buffer);

    auto RhiCreateCmdList(RhiQueueType queueType) -> RhiCmdList;
    void RhiNameCmdList(RhiCmdList, std::string_view name);
    void RhiDestroyCmdList(RhiCmdList cmd);

    auto RhiCmdSetCheckpoint(RhiCmdList cmd, uint64_t data) -> uint64_t;
    auto RhiGetLastCheckpointData(RhiQueueType queueType) -> uint64_t;

    auto RhiFrameBegin() -> RhiCmdList;
    void RhiFrameEnd();

    auto RhiFrameGetCmdList() -> RhiCmdList;

    auto RhiCreateDescriptorSetLayout(const RhiDescriptorSetLayoutDesc &) -> RhiDescriptorSetLayout;
    void RhiDestroyDescriptorSetLayout(RhiDescriptorSetLayout);

    auto RhiCreateDescriptorSet(RhiDescriptorSetLayout) -> RhiDescriptorSet;
    void RhiDestroyDescriptorSet(RhiDescriptorSet);
    void RhiWriteDescriptors(std::span<const RhiDescriptorWriteDesc>);

    void RhiPassBegin(RhiPassDesc);
    void RhiPassEnd(RhiPassDesc);

    auto RhiGetVertexFormatSize(RhiVertexFormat) -> uint32_t;

    auto RhiCreateGraphicsPipeline(const RhiGraphicsPipelineDesc &) -> RhiGraphicsPipeline;
    void RhiNameGraphicsPipeline(RhiGraphicsPipeline, std::string_view name);
    void RhiDestroyGraphicsPipeline(RhiGraphicsPipeline);

    void RhiCmdBindGraphicsPipeline(RhiCmdList, RhiGraphicsPipeline);
    void RhiCmdBindVertexBuffers(RhiCmdList cmd, uint32_t firstBinding, std::span<const RhiBuffer> buffers,
                                 std::span<const uint32_t> offsets);
    void RhiCmdBindGraphicsBindGroup(RhiCmdList, uint32_t setIndex, RhiDescriptorSet bindGroup,
                                     std::span<const uint32_t> dynamicOffsets);
    void RhiCmdPushGraphicsConstants(RhiCmdList cmd, uint32_t offset, RhiShaderStage stage, ByteView data);
    void RhiCmdDraw(RhiCmdList cmd, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
                    uint32_t firstInstance);

    auto RhiCreateSampler(const RhiSamplerDesc &) -> RhiSampler;
    void RhiDestroySampler(RhiSampler);

    auto RhiCreateShader(const RhiShaderDesc &) -> RhiShader;
    void RhiDestroyShader(RhiShader);

    auto RhiCreateTexture(RhiTextureDesc) -> RhiTexture;
    void RhiDestroyTexture(RhiTexture);
    auto RhiGetTextureInfo(RhiTexture) -> RhiTextureInfo;
    void RhiCmdTransitionTexture(RhiCmdList, RhiTexture, RhiTextureState);
    void RhiCmdCopyTexture(RhiCmdList cmd, RhiTexture dst, RhiBuffer src, uint32_t srcOffset, uint32_t size);

    auto RhiGetBackbufferTexture() -> RhiTexture;

  private:
    class Impl;
    Impl *impl;
};
extern Rhi *g_Rhi;

} // namespace nyla