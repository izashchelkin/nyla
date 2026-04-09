#pragma once

#include <cstdint>

#include "nyla/commons/bitenum.h"
#include "nyla/commons/handle.h"
#include "nyla/commons/inline_string.h"
#include "nyla/commons/span.h"

namespace nyla
{

//

enum class RhiFlags : uint32_t
{
    VSync = 1 << 0,
};
NYLA_BITENUM(RhiFlags);

enum class RhiBufferUsage : uint32_t
{
    Vertex = 1 << 0,
    Index = 1 << 1,
    Uniform = 1 << 2,
    CopySrc = 1 << 3,
    CopyDst = 1 << 4,
};
NYLA_BITENUM(RhiBufferUsage);

enum class RhiMemoryUsage
{
    Unknown = 0,
    GpuOnly,
    CpuToGpu,
    GpuToCpu
};

enum class RhiBufferState
{
    Undefined = 0,
    CopySrc,
    CopyDst,
    ShaderRead,
    ShaderWrite,
    Vertex,
    Index,
    Indirect,
};

enum class RhiQueueType
{
    Graphics,
    Transfer
};

enum class RhiVertexFormat
{
    None,
    R32G32B32A32Float,
    R32G32B32Float,
    R32G32Float,
};

enum class RhiInputRate
{
    PerVertex,
    PerInstance
};

enum class RhiCullMode
{
    None,
    Back,
    Front
};

enum class RhiFrontFace
{
    CCW,
    CW
};

enum class RhiFilter
{
    Linear,
    Nearest,
};

enum class RhiSamplerAddressMode
{
    Repeat,
    ClampToEdge,
};

enum class RhiShaderStage : uint32_t
{
    Vertex = 1 << 0,
    Pixel = 1 << 1,
};

enum class RhiTextureFormat
{
    None,

    R8_UNORM,

    R8G8B8A8_sRGB,
    B8G8R8A8_sRGB,

    D32_Float,
    D32_Float_S8_UINT,
};

enum class RhiTextureUsage
{
    None = 0,

    ShaderSampled = 1 << 0,
    ColorTarget = 1 << 1,
    DepthStencil = 1 << 2,
    TransferSrc = 1 << 3,
    TransferDst = 1 << 4,
    Storage = 1 << 5,
    Present = 1 << 6,
};
NYLA_BITENUM(RhiTextureUsage);

enum class RhiTextureState
{
    Undefined,
    ColorTarget,
    DepthTarget,
    ShaderRead,
    Present,
    TransferSrc,
    TransferDst,
};

//

struct RhiTexture : Handle
{
};

struct RhiSampledTextureView : Handle
{
};

struct RhiRenderTargetView : Handle
{
};

struct RhiDepthStencilView : Handle
{
};

struct RhiBuffer : Handle
{
};

struct RhiCmdList : Handle
{
};

struct RhiGraphicsPipeline : Handle
{
};

struct RhiSampler : Handle
{
};

struct RhiShader : Handle
{
};

//

constexpr inline uint32_t kRhiMaxNumFramesInFlight = 3;
constexpr inline uint32_t kRhiMaxNumSwapchainTextures = 3;

#if defined(NDEBUG)
constexpr inline bool kRhiValidations = false;
#else
constexpr inline bool kRhiValidations = true;
#endif

constexpr inline bool kRhiCheckpoints = false;

//

struct RhiLimits
{
    uint32_t numTextures = 64;
    uint32_t numTextureViews = 64;

    uint32_t numBuffers = 16;

    uint32_t numSamplers = 8;

    uint32_t numFramesInFlight = 2;
    uint32_t maxDrawCount = 1024;
    uint32_t maxPassCount = 4;

    uint32_t frameConstantSize = 256;
    uint32_t passConstantSize = 512;
    uint32_t drawConstantSize = 256;
    uint32_t largeDrawConstantSize = 1024;
};

struct RhiInitDesc
{
    RhiFlags flags;
    RhiLimits limits;
};

struct RhiBufferDesc
{
    uint64_t size;
    RhiBufferUsage bufferUsage;
    RhiMemoryUsage memoryUsage;
};

struct RhiPassDesc
{
    RhiRenderTargetView rtv;
    RhiDepthStencilView dsv;
};

struct RhiVertexBindingDesc
{
    uint32_t binding;
    uint32_t stride;
    RhiInputRate inputRate;
};

struct RhiVertexAttributeDesc
{
    uint32_t binding;
    inline_string<16> semantic;
    RhiVertexFormat format;
    uint32_t offset;
};

struct RhiGraphicsPipelineDesc
{
    byteview debugName;

    RhiShader vs;
    RhiShader ps;

    span<RhiVertexBindingDesc> vertexBindings;
    span<RhiVertexAttributeDesc> vertexAttributes;
    span<RhiTextureFormat> colorTargetFormats;
    RhiTextureFormat depthFormat;
    bool depthWriteEnabled;
    bool depthTestEnabled;

    RhiCullMode cullMode;
    RhiFrontFace frontFace;
};

struct RhiSamplerDesc
{
    RhiFilter minFilter;
    RhiFilter magFilter;
    RhiSamplerAddressMode addressModeU;
    RhiSamplerAddressMode addressModeV;
    RhiSamplerAddressMode addressModeW;
};

template <> struct EnableBitMaskOps<RhiShaderStage> : std::true_type
{
};

struct RhiShaderDesc
{
    RhiShaderStage stage;
    span<uint32_t> code;
};

struct RhiTextureDesc
{
    uint32_t width;
    uint32_t height;
    RhiMemoryUsage memoryUsage;
    RhiTextureUsage usage;
    RhiTextureFormat format;
};

struct RhiTextureViewDesc
{
    RhiTexture texture;
    RhiTextureFormat format;
};

struct RhiRenderTargetViewDesc
{
    RhiTexture texture;
    RhiTextureFormat format;
};

struct RhiDepthStencilViewDesc
{
    RhiTexture texture;
    RhiTextureFormat format;
};

struct RhiTextureInfo
{
    uint32_t width;
    uint32_t height;
    RhiTextureFormat format;
};

class Rhi
{
  public:
    static void Init(const RhiInitDesc &);
    static auto GetNumFramesInFlight() -> uint32_t;
    static auto GetFrameIndex() -> uint32_t;
    static auto GetMinUniformBufferOffsetAlignment() -> uint32_t;
    static auto GetOptimalBufferCopyOffsetAlignment() -> uint32_t;

    static auto CreateBuffer(const RhiBufferDesc &) -> RhiBuffer;
    static void NameBuffer(RhiBuffer, byteview name);
    static void DestroyBuffer(RhiBuffer);

    static auto GetBufferSize(RhiBuffer) -> uint64_t;

    static auto MapBuffer(RhiBuffer) -> char *;
    static void UnmapBuffer(RhiBuffer);
    static void BufferMarkWritten(RhiBuffer, uint32_t offset, uint32_t size);

    static void CmdCopyBuffer(RhiCmdList cmd, RhiBuffer dst, uint32_t dstOffset, RhiBuffer src, uint32_t srcOffset,
                              uint32_t size);
    static void CmdTransitionBuffer(RhiCmdList cmd, RhiBuffer buffer, RhiBufferState newState);
    static void CmdUavBarrierBuffer(RhiCmdList cmd, RhiBuffer buffer);

    static auto CreateCmdList(RhiQueueType queueType) -> RhiCmdList;
    static void NameCmdList(RhiCmdList, byteview name);
    static void DestroyCmdList(RhiCmdList cmd);
    static void ResetCmdList(RhiCmdList cmd);

    static auto CmdSetCheckpoint(RhiCmdList cmd, uint64_t data) -> uint64_t;
    static auto GetLastCheckpointData(RhiQueueType queueType) -> uint64_t;

    static auto FrameBegin() -> RhiCmdList;
    static void FrameEnd();

    static auto FrameGetCmdList() -> RhiCmdList;

    static void PassBegin(RhiPassDesc);
    static void PassEnd();

    static auto GetVertexFormatSize(RhiVertexFormat) -> uint32_t;

    static auto CreateGraphicsPipeline(const RhiGraphicsPipelineDesc &) -> RhiGraphicsPipeline;
    static void NameGraphicsPipeline(RhiGraphicsPipeline, byteview name);
    static void DestroyGraphicsPipeline(RhiGraphicsPipeline);

    static void CmdBindGraphicsPipeline(RhiCmdList, RhiGraphicsPipeline);
    static void CmdBindVertexBuffers(RhiCmdList cmd, uint32_t firstBinding, span<const RhiBuffer> buffers,
                                     span<const uint64_t> offsets);
    static void CmdBindIndexBuffer(RhiCmdList cmd, RhiBuffer buffer, uint64_t offset);
    static void CmdPushGraphicsConstants(RhiCmdList cmd, uint32_t offset, RhiShaderStage stage, byteview data);
    static void CmdDraw(RhiCmdList cmd, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
                        uint32_t firstInstance);
    static void CmdDrawIndexed(RhiCmdList cmd, uint32_t indexCount, int32_t vertexOffset, uint32_t instanceCount,
                               uint32_t firstIndex, uint32_t firstInstance);

    static auto CreateSampler(const RhiSamplerDesc &) -> RhiSampler;
    static void DestroySampler(RhiSampler);

    static auto CreateShader(const RhiShaderDesc &) -> RhiShader;
    static void DestroyShader(RhiShader);

    static auto CreateTexture(const RhiTextureDesc &) -> RhiTexture;
    static void DestroyTexture(RhiTexture);
    static auto GetTextureInfo(RhiTexture) -> RhiTextureInfo;
    static void CmdTransitionTexture(RhiCmdList, RhiTexture, RhiTextureState);
    static void CmdCopyTexture(RhiCmdList cmd, RhiTexture dst, RhiBuffer src, uint32_t srcOffset, uint32_t size);
    static void CmdCopyTexture(RhiCmdList cmd, RhiTexture dst, RhiTexture src);

    static auto CreateSampledTextureView(const RhiTextureViewDesc &) -> RhiSampledTextureView;
    static void DestroySampledTextureView(RhiSampledTextureView);
    static auto GetTexture(RhiSampledTextureView srv) -> RhiTexture;

    static auto CreateRenderTargetView(const RhiRenderTargetViewDesc &) -> RhiRenderTargetView;
    static void DestroyRenderTargetView(RhiRenderTargetView);
    static auto GetTexture(RhiRenderTargetView srv) -> RhiTexture;

    static auto CreateDepthStencilView(const RhiDepthStencilViewDesc &desc) -> RhiDepthStencilView;
    static void DestroyDepthStencilView(RhiDepthStencilView textureView);
    static auto GetTexture(RhiDepthStencilView dsv) -> RhiTexture;

    static auto GetBackbufferView() -> RhiRenderTargetView;
    static void TriggerSwapchainRecreate();

    static void SetFrameConstant(RhiCmdList cmd, byteview data);
    static void SetPassConstant(RhiCmdList cmd, byteview data);
    static void SetDrawConstant(RhiCmdList cmd, byteview data);
    static void SetLargeDrawConstant(RhiCmdList cmd, byteview data);
};

} // namespace nyla