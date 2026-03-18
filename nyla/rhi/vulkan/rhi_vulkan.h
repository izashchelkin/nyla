#pragma once

#include <cstdint>
#include <string_view>

#include "nyla/commons/align.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/log.h"
#include "nyla/rhi/rhi.h"
#include "nyla/spirview/spirview.h"
#include "vulkan/vk_enum_string_helper.h"
#include "vulkan/vulkan_core.h"

namespace nyla
{

// class Rhi::Impl
// {
//   public:
//     auto CreateTimeline(uint64_t initialValue) -> VkSemaphore;
//     void WaitTimeline(VkSemaphore timeline, uint64_t waitValue);
//
//     auto FindMemoryTypeIndex(VkMemoryRequirements memRequirements, VkMemoryPropertyFlags properties) -> uint32_t;
//
//     void VulkanNameHandle(VkObjectType type, uint64_t handle, std::string_view name);
//
//     auto ConvertBufferUsageIntoVkBufferUsageFlags(RhiBufferUsage usage) -> VkBufferUsageFlags;
//
//     auto ConvertMemoryUsageIntoVkMemoryPropertyFlags(RhiMemoryUsage usage) -> VkMemoryPropertyFlags;
//
//     auto ConvertVertexFormatIntoVkFormat(RhiVertexFormat format) -> VkFormat;
//
//     auto ConvertTextureFormatIntoVkFormat(RhiTextureFormat format, VkImageAspectFlags *outAspectMask) -> VkFormat;
//     auto ConvertVkFormatIntoTextureFormat(VkFormat format) -> RhiTextureFormat;
//
//     auto ConvertTextureUsageToVkImageUsageFlags(RhiTextureUsage usage) -> VkImageUsageFlags;
//
//     auto ConvertShaderStageIntoVkShaderStageFlags(RhiShaderStage stageFlags) -> VkShaderStageFlags;
//
//     auto DebugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
//                                 VkDebugUtilsMessageTypeFlagsEXT messageType,
//                                 const VkDebugUtilsMessengerCallbackDataEXT *callbackData) -> VkBool32;
//
//     //
//
//     void Init(const RhiInitDesc &);
//     auto GetMinUniformBufferOffsetAlignment() -> uint32_t;
//     auto GetOptimalBufferCopyOffsetAlignment() -> uint32_t;
//
//     auto FrameBegin() -> RhiCmdList;
//     void FrameEnd();
//     auto GetNumFramesInFlight() -> uint32_t
//     {
//         return m_Limits.numFramesInFlight;
//     }
//     auto GetFrameIndex() -> uint32_t
//     {
//         return m_FrameIndex;
//     }
//     auto FrameGetCmdList() -> RhiCmdList;
//
//     auto CreateSampler(const RhiSamplerDesc &desc) -> RhiSampler;
//     void DestroySampler(RhiSampler sampler);
//
//     auto CreateBuffer(const RhiBufferDesc &desc) -> RhiBuffer;
//     void NameBuffer(RhiBuffer buf, std::string_view name);
//     void DestroyBuffer(RhiBuffer buffer);
//     auto GetBufferSize(RhiBuffer buffer) -> uint64_t;
//     auto MapBuffer(RhiBuffer buffer) -> char *;
//     void UnmapBuffer(RhiBuffer buffer);
//     void CmdTransitionBuffer(RhiCmdList cmd, RhiBuffer buffer, RhiBufferState newState);
//     void CmdCopyBuffer(RhiCmdList cmd, RhiBuffer dst, uint32_t dstOffset, RhiBuffer src, uint32_t srcOffset,
//                        uint32_t size);
//     void CmdUavBarrierBuffer(RhiCmdList cmd, RhiBuffer buffer);
//     void BufferMarkWritten(RhiBuffer buffer, uint32_t offset, uint32_t size);
//
//     auto CreateTexture(RhiTextureDesc desc) -> RhiTexture;
//     auto GetTextureInfo(RhiTexture texture) -> RhiTextureInfo;
//     void DestroyTexture(RhiTexture texture);
//     void CmdTransitionTexture(RhiCmdList cmd, RhiTexture texture, RhiTextureState newState);
//     void CmdCopyTexture(RhiCmdList cmd, RhiTexture dst, RhiBuffer src, uint32_t srcOffset, uint32_t size);
//     void CmdCopyTexture(RhiCmdList cmd, RhiTexture dst, RhiTexture src);
//
//     auto CreateSampledTextureView(const RhiTextureViewDesc &desc) -> RhiSampledTextureView;
//     void DestroySampledTextureView(RhiSampledTextureView textureView);
//     auto GetTexture(RhiSampledTextureView srv) -> RhiTexture;
//
//     auto CreateRenderTargetView(const RhiRenderTargetViewDesc &desc) -> RhiRenderTargetView;
//     void DestroyRenderTargetView(RhiRenderTargetView textureView);
//     auto GetTexture(RhiRenderTargetView rtv) -> RhiTexture;
//
//     auto CreateDepthStencilView(const RhiDepthStencilViewDesc &desc) -> RhiDepthStencilView;
//     void DestroyDepthStencilView(RhiDepthStencilView textureView);
//     auto GetTexture(RhiDepthStencilView dsv) -> RhiTexture;
//
//     void EnsureHostWritesVisible(VkCommandBuffer cmdbuf, VulkanBufferData &bufferData);
//
//     auto CreateCmdList(RhiQueueType queueType) -> RhiCmdList;
//     void NameCmdList(RhiCmdList cmd, std::string_view name);
//     void DestroyCmdList(RhiCmdList cmd);
//     void ResetCmdList(RhiCmdList cmd);
//     auto CmdSetCheckpoint(RhiCmdList cmd, uint64_t data) -> uint64_t;
//     auto GetLastCheckpointData(RhiQueueType queueType) -> uint64_t;
//     auto GetDeviceQueue(RhiQueueType queueType) -> DeviceQueue &;
//
//     void PassBegin(RhiPassDesc desc);
//     void PassEnd();
//
//     auto CreateShader(const RhiShaderDesc &desc) -> RhiShader;
//     void DestroyShader(RhiShader shader);
//
//     auto CreateGraphicsPipeline(const RhiGraphicsPipelineDesc &desc) -> RhiGraphicsPipeline;
//     void NameGraphicsPipeline(RhiGraphicsPipeline pipeline, std::string_view name);
//     void DestroyGraphicsPipeline(RhiGraphicsPipeline pipeline);
//     void CmdBindGraphicsPipeline(RhiCmdList cmd, RhiGraphicsPipeline pipeline);
//     void CmdPushGraphicsConstants(RhiCmdList cmd, uint32_t offset, RhiShaderStage stage, ByteView data);
//     void CmdBindVertexBuffers(RhiCmdList cmd, uint32_t firstBinding, std::span<const RhiBuffer> buffers,
//                               std::span<const uint64_t> offsets);
//     void CmdBindIndexBuffer(RhiCmdList cmd, RhiBuffer buffer, uint64_t offset);
//     void CmdDraw(RhiCmdList cmd, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
//                  uint32_t firstInstance);
//     void CmdDrawIndexed(RhiCmdList cmd, uint32_t indexCount, int32_t vertexOffset, uint32_t instanceCount,
//                         uint32_t firstIndex, uint32_t firstInstance);
//     auto GetVertexFormatSize(RhiVertexFormat format) -> uint32_t;
//
// #if 0
//     auto CreateDescriptorSetLayout(const RhiDescriptorSetLayoutDesc &desc) -> RhiDescriptorSetLayout;
//     void DestroyDescriptorSetLayout(RhiDescriptorSetLayout layout);
//     auto CreateDescriptorSet(RhiDescriptorSetLayout layout) -> RhiDescriptorSet;
//     void WriteDescriptors(std::span<const RhiDescriptorWriteDesc> writes);
//     void DestroyDescriptorSet(RhiDescriptorSet bindGroup);
//     void CmdBindGraphicsBindGroup(RhiCmdList cmd, uint32_t setIndex, RhiDescriptorSet bindGroup,
//                                   std::span<const uint32_t> dynamicOffsets);
// #endif
//
//     void TriggerSwapchainRecreate()
//     {
//         m_RecreateSwapchain = true;
//     }
//     void CreateSwapchain();
//     auto GetBackbufferView() -> RhiRenderTargetView;
//
//     void WriteDescriptorTables();
//     void BindDescriptorTables(RhiCmdList cmd);
//
//     void SetFrameConstant(RhiCmdList cmd, std::span<const std::byte> data);
//     void SetPassConstant(RhiCmdList cmd, std::span<const std::byte> data);
//     void SetDrawConstant(RhiCmdList cmd, std::span<const std::byte> data);
//     void SetLargeDrawConstant(RhiCmdList cmd, std::span<const std::byte> data);
//
//   private:
// };

} // namespace nyla