#pragma once

#include <cstdint>

#include "nyla/commons/bitenum.h"
#include "nyla/commons/handle.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

enum class rhi_flags : uint32_t
{
    VSync = 1 << 0,
};
NYLA_BITENUM(rhi_flags);

enum class rhi_buffer_usage : uint32_t
{
    Vertex = 1 << 0,
    Index = 1 << 1,
    Uniform = 1 << 2,
    CopySrc = 1 << 3,
    CopyDst = 1 << 4,
};
NYLA_BITENUM(rhi_buffer_usage);

enum class rhi_memory_usage
{
    Unknown = 0,
    GpuOnly,
    CpuToGpu,
    GpuToCpu
};

enum class rhi_buffer_state
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

enum class rhi_queue_type
{
    Graphics,
    Transfer
};

enum class rhi_vertex_format
{
    None,
    R32G32B32A32Float,
    R32G32B32Float,
    R32G32Float,
};

enum class rhi_input_rate
{
    PerVertex,
    PerInstance
};

enum class rhi_cull_mode
{
    None,
    Back,
    Front
};

enum class rhi_front_face
{
    CCW,
    CW
};

enum class rhi_filter
{
    Linear,
    Nearest,
};

enum class rhi_sampler_address_mode
{
    Repeat,
    ClampToEdge,
};

enum class rhi_shader_stage : uint32_t
{
    Vertex = 1 << 0,
    Pixel = 1 << 1,
};

enum class rhi_texture_format
{
    None,

    R8_UNORM,

    R8G8B8A8_sRGB,
    B8G8R8A8_sRGB,

    D32_Float,
    D32_Float_S8_UINT,
};

enum class rhi_texture_usage
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
NYLA_BITENUM(rhi_texture_usage);

enum class rhi_texture_state
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

struct rhi_texture : handle
{
};

struct rhi_srv : handle
{
};

struct rhi_rtv : handle
{
};

struct rhi_dsv : handle
{
};

struct rhi_buffer : handle
{
};

struct rhi_cmdlist : handle
{
};

struct rhi_graphics_pipeline : handle
{
};

struct rhi_sampler : handle
{
};

struct rhi_shader : handle
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

struct rhi_limits
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

struct rhi_init_desc
{
    rhi_flags flags;
    rhi_limits limits;
};

struct rhi_buffer_desc
{
    uint64_t size;
    rhi_buffer_usage bufferUsage;
    rhi_memory_usage memoryUsage;
};

struct rhi_pass_desc
{
    rhi_rtv rtv;
    rhi_dsv dsv;
};

struct rhi_vertex_binding_desc
{
    uint32_t binding;
    uint32_t stride;
    rhi_input_rate inputRate;
};

struct rhi_vertex_attribute_desc
{
    uint32_t binding;
    byteview semantic;
    rhi_vertex_format format;
    uint32_t offset;
};

struct rhi_graphics_pipeline_desc
{
    byteview debugName;

    rhi_shader vs;
    rhi_shader ps;

    span<rhi_vertex_binding_desc> vertexBindings;
    span<rhi_vertex_attribute_desc> vertexAttributes;
    span<rhi_texture_format> colorTargetFormats;
    rhi_texture_format depthFormat;
    bool depthWriteEnabled;
    bool depthTestEnabled;

    rhi_cull_mode cullMode;
    rhi_front_face frontFace;
};

struct rhi_sampler_desc
{
    rhi_filter minFilter;
    rhi_filter magFilter;
    rhi_sampler_address_mode addressModeU;
    rhi_sampler_address_mode addressModeV;
    rhi_sampler_address_mode addressModeW;
};

template <> struct EnableBitMaskOps<rhi_shader_stage> : std::true_type
{
};

struct rhi_shader_desc
{
    rhi_shader_stage stage;
    span<uint32_t> code;
};

struct rhi_texture_desc
{
    uint32_t width;
    uint32_t height;
    rhi_memory_usage memoryUsage;
    rhi_texture_usage usage;
    rhi_texture_format format;
};

struct rhi_texture_view_desc
{
    rhi_texture texture;
    rhi_texture_format format;
};

struct rhi_render_target_view_desc
{
    rhi_texture texture;
    rhi_texture_format format;
};

struct rhi_depth_stencil_view_desc
{
    rhi_texture texture;
    rhi_texture_format format;
};

struct rhi_texture_info
{
    uint32_t width;
    uint32_t height;
    rhi_texture_format format;
};

namespace Rhi
{

void API Bootstrap(region_alloc &alloc, const rhi_init_desc &);
auto API GetNumFramesInFlight() -> uint32_t;
auto API GetFrameIndex() -> uint32_t;
auto API GetMinUniformBufferOffsetAlignment() -> uint32_t;
auto API GetOptimalBufferCopyOffsetAlignment() -> uint32_t;

auto API CreateBuffer(const rhi_buffer_desc &) -> rhi_buffer;
void API NameBuffer(rhi_buffer, byteview name);
void API DestroyBuffer(rhi_buffer);

auto API GetBufferSize(rhi_buffer) -> uint64_t;

auto API MapBuffer(rhi_buffer) -> char *;
void API UnmapBuffer(rhi_buffer);
void API BufferMarkWritten(rhi_buffer, uint32_t offset, uint32_t size);

void API CmdCopyBuffer(rhi_cmdlist cmd, rhi_buffer dst, uint32_t dstOffset, rhi_buffer src, uint32_t srcOffset,
                       uint32_t size);
void API CmdTransitionBuffer(rhi_cmdlist cmd, rhi_buffer buffer, rhi_buffer_state newState);
void API CmdUavBarrierBuffer(rhi_cmdlist cmd, rhi_buffer buffer);

auto API CreateCmdList(rhi_queue_type queueType) -> rhi_cmdlist;
void API NameCmdList(rhi_cmdlist, byteview name);
void API DestroyCmdList(rhi_cmdlist cmd);
void API ResetCmdList(rhi_cmdlist cmd);

auto API CmdSetCheckpoint(rhi_cmdlist cmd, uint64_t data) -> uint64_t;
auto API GetLastCheckpointData(rhi_queue_type queueType) -> uint64_t;

auto API FrameBegin(region_alloc &scratch) -> rhi_cmdlist;
void API FrameEnd(region_alloc &scratch);

void API PassBegin(rhi_pass_desc);
void API PassEnd();

auto API GetVertexFormatSize(rhi_vertex_format) -> uint32_t;

auto API CreateGraphicsPipeline(region_alloc &scratch, const rhi_graphics_pipeline_desc &) -> rhi_graphics_pipeline;
void API NameGraphicsPipeline(rhi_graphics_pipeline, byteview name);
void API DestroyGraphicsPipeline(rhi_graphics_pipeline);

void API CmdBindGraphicsPipeline(rhi_cmdlist, rhi_graphics_pipeline);
void API CmdBindVertexBuffers(rhi_cmdlist cmd, uint32_t firstBinding, span<const rhi_buffer> buffers,
                              span<const uint64_t> offsets);
void API CmdBindIndexBuffer(rhi_cmdlist cmd, rhi_buffer buffer, uint64_t offset);
void API CmdPushGraphicsConstants(rhi_cmdlist cmd, uint32_t offset, rhi_shader_stage stage, byteview data);
void API CmdDraw(rhi_cmdlist cmd, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
                 uint32_t firstInstance);
void API CmdDrawIndexed(rhi_cmdlist cmd, uint32_t indexCount, int32_t vertexOffset, uint32_t instanceCount,
                        uint32_t firstIndex, uint32_t firstInstance);

auto API CreateSampler(const rhi_sampler_desc &) -> rhi_sampler;
void API DestroySampler(rhi_sampler);

auto API CreateShader(const rhi_shader_desc &) -> rhi_shader;
void API DestroyShader(rhi_shader);

auto API CreateTexture(const rhi_texture_desc &) -> rhi_texture;
void API DestroyTexture(rhi_texture);
auto API GetTextureInfo(rhi_texture) -> rhi_texture_info;
void API CmdTransitionTexture(rhi_cmdlist, rhi_texture, rhi_texture_state);
void API CmdCopyTexture(rhi_cmdlist cmd, rhi_texture dst, rhi_buffer src, uint32_t srcOffset, uint32_t size);
void API CmdCopyTexture(rhi_cmdlist cmd, rhi_texture dst, rhi_texture src);

auto API CreateSampledTextureView(const rhi_texture_view_desc &) -> rhi_srv;
void API DestroySampledTextureView(rhi_srv);
auto API GetTexture(rhi_srv srv) -> rhi_texture;

auto API CreateRenderTargetView(const rhi_render_target_view_desc &) -> rhi_rtv;
void API DestroyRenderTargetView(rhi_rtv);
auto API GetTexture(rhi_rtv srv) -> rhi_texture;

auto API CreateDepthStencilView(const rhi_depth_stencil_view_desc &desc) -> rhi_dsv;
void API DestroyDepthStencilView(rhi_dsv textureView);
auto API GetTexture(rhi_dsv dsv) -> rhi_texture;

auto API GetBackbufferView() -> rhi_rtv;
void API TriggerSwapchainRecreate();

void API SetFrameConstant(rhi_cmdlist cmd, byteview data);
void API SetPassConstant(rhi_cmdlist cmd, byteview data);
void API SetDrawConstant(rhi_cmdlist cmd, byteview data);
void API SetLargeDrawConstant(rhi_cmdlist cmd, byteview data);

}; // namespace Rhi

} // namespace nyla