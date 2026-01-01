#pragma once

#include "nyla/rhi/rhi.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

namespace nyla
{

template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

namespace rhi_d3d12_internal
{
} // namespace rhi_d3d12_internal
using namespace rhi_d3d12_internal;

class Rhi::Impl
{
  public:
    void CreateSwapchain();

    //

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

    auto CreateTexture(RhiTextureDesc) -> RhiTexture;
    void DestroyTexture(RhiTexture);
    auto GetTextureInfo(RhiTexture) -> RhiTextureInfo;
    void CmdTransitionTexture(RhiCmdList, RhiTexture, RhiTextureState);
    void CmdCopyTexture(RhiCmdList cmd, RhiTexture dst, RhiBuffer src, uint32_t srcOffset, uint32_t size);

    auto GetBackbufferTexture() -> RhiTexture;

  private:
    RhiFlags m_Flags{};
    uint32_t m_NumFramesInFlight{};
    HWND m_Window{};

    uint32_t m_FrameIndex{};

    ComPtr<IDXGIAdapter1> m_Adapter;
    ComPtr<ID3D12Device> m_Device;

    ComPtr<IDXGIFactory2> m_Factory;
    ComPtr<IDXGISwapChain3> m_Swapchain;

    ComPtr<ID3D12CommandQueue> m_DirectCommandQueue;

    ComPtr<ID3D12DescriptorHeap> m_RtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_CbvHeap;
    ComPtr<ID3D12DescriptorHeap> m_DsvHeap;
    ComPtr<ID3D12QueryHeap> m_QueryHeap;

    struct FrameContext
    {
        ComPtr<ID3D12CommandAllocator> cmdAlloc;
        ComPtr<ID3D12CommandList> cmd;
    };
    std::array<FrameContext, kRhiMaxNumFramesInFlight> frameContext;

    uint32_t m_RTVDescriptorSize;
    uint32_t m_CBVDescriptorSize;
};

} // namespace nyla