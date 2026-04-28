#include <cstdint>

#include "assets.h"
#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/asset_manager.h"
#include "nyla/commons/byteliterals.h"
#include "nyla/commons/cell_renderer.h"
#include "nyla/commons/debug_text_renderer.h"
#include "nyla/commons/dev_assets.h"
#include "nyla/commons/dev_log.h"
#include "nyla/commons/dev_shaders.h"
#include "nyla/commons/engine.h"
#include "nyla/commons/entrypoint.h"
#include "nyla/commons/file.h"
#include "nyla/commons/gamepad.h"
#include "nyla/commons/gpu_upload.h"
#include "nyla/commons/input_manager.h"
#include "nyla/commons/macros.h" // IWYU pragma: keep
#include "nyla/commons/math.h"
#include "nyla/commons/mesh_manager.h"
#include "nyla/commons/minmax.h"
#include "nyla/commons/pipeline_cache.h"
#include "nyla/commons/profiler.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/render_targets.h"
#include "nyla/commons/renderer.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/sampler_manager.h"
#include "nyla/commons/shader.h"
#include "nyla/commons/texture_manager.h"
#include "nyla/commons/tunables.h"
#include "nyla/commons/tween_manager.h"
#include "nyla/commons/vec.h"

namespace nyla
{

void UserMain()
{
    region_alloc alloc = RegionAlloc::Create(16_MiB, 0);

    Engine::Bootstrap(alloc, engine_init_desc{
                                 .maxFps = 144,
                                 .vsync = true,
                             });

    AssetManager::Bootstrap(FileOpen(R"(assets.bin)"_s, FileOpenMode::Read));
    GpuUpload::Bootstrap();
    SamplerManager::Bootstrap();
    TextureManager::Bootstrap();
    MeshManager::Bootstrap();
    TweenManager::Bootstrap();
#if !defined(NDEBUG)
    {
        DevLog::Bootstrap();

        const byteview devRoots[] = {"assets"_s, "asset_public"_s};
        DevAssets::Bootstrap(span<const byteview>{devRoots, 2});

        const dev_shader_root shaderRoots[] = {
            {.srcDir = "nyla/shaders"_s, .outDir = "asset_public/shaders"_s},
        };
        DevShaders::Bootstrap(span<const dev_shader_root>{shaderRoots, 1});
    }
#endif
    Shader::Bootstrap();
    PipelineCache::Bootstrap();
    DebugTextRenderer::Bootstrap(alloc);
    Renderer::Bootstrap(alloc);
#if !defined(NDEBUG)
    CellRenderer::Bootstrap(alloc, cell_renderer_init_desc{
                                       .bdfGuid = 0x30B510FE27A113FB,
                                   });
    Tunables::Bootstrap("3d_ball_maze.tunables"_s);
#endif

    static float moveSpeed = 10.f;
    static float fovDeg = 90.f;
    static float lookSensitivity = 0.1f;
#if !defined(NDEBUG)
    Tunables::RegisterFloat("camera.moveSpeed"_s, &moveSpeed, 0.5f, 0.5f, 100.f);
    Tunables::RegisterFloat("camera.fovDeg"_s, &fovDeg, 1.f, 30.f, 170.f);
    Tunables::RegisterFloat("camera.lookSens"_s, &lookSensitivity, 0.01f, 0.01f, 1.f);
    Profiler::Bootstrap();
#endif

    mesh_handle cubeMesh = MeshManager::DeclareMesh(ID_mesh_gltf_cube, ID_mesh_bin_cube);
    mesh_handle sphereMesh = MeshManager::DeclareMesh(ID_mesh_gltf_sphere, ID_mesh_bin_sphere);
    mesh_handle rectMesh = MeshManager::DeclareMesh(ID_mesh_rect_gltf, ID_mesh_bin_rect);

    render_targets renderTargets{
        .ColorFormat = rhi_texture_format::B8G8R8A8_sRGB,
        .DepthStencilFormat = rhi_texture_format::D32_Float_S8_UINT,
    };

    while (!Engine::ShouldExit())
    {
        engine_frame frame = Engine::FrameBegin(alloc);

        DebugTextRenderer::Fmt(500, 10, "fps=%d"_s, uint32_t(frame.fps));

        GpuUpload::Update();
        InputManager::Update();
        TweenManager::Update(frame.dt);
        MeshManager::Update(alloc, frame.cmd);
        TextureManager::Update(frame.cmd);

        {
            rhi_texture backbuffer = Rhi::GetTexture(Rhi::GetBackbufferView());
            rhi_texture_info backbufferInfo = Rhi::GetTextureInfo(backbuffer);

            rhi_rtv rtv;
            rhi_dsv dsv;
            RenderTargets::GetTargets(renderTargets, backbufferInfo.width, backbufferInfo.height, &rtv, &dsv);

            {
                float2 movement{};

                static float yaw = 0;
                static float pitch = 0;

                float verticalInput = 0;

                if (UpdateGamepad(0))
                {
                    movement = GetGamepadLeftStick(0);

                    const float2 rotation = GetGamepadRightStick(0);
                    yaw += rotation[0] * lookSensitivity;
                    pitch -= rotation[1] * lookSensitivity;

                    pitch = Clamp(pitch, -1.55f, 1.55f);

                    if (yaw > 2 * math::pi)
                        yaw -= 2 * math::pi;
                    if (yaw < -2 * math::pi)
                        yaw += 2 * math::pi;

                    verticalInput = GetGamepadLeftTrigger(0) - GetGamepadRightTrigger(0);
                }

                rhi_texture renderTarget = Rhi::GetTexture(rtv);
                rhi_texture_info rtInfo = Rhi::GetTextureInfo(renderTarget);
                Rhi::CmdTransitionTexture(frame.cmd, renderTarget, rhi_texture_state::ColorTarget);

                Rhi::CmdTransitionTexture(frame.cmd, Rhi::GetTexture(dsv), rhi_texture_state::DepthTarget);

                Rhi::PassBegin({
                    .rtv = rtv,
                    .dsv = dsv,
                });
                {
                    Renderer::Mesh({0, 0, 0}, {1, 1, 1}, sphereMesh, {});
                    Renderer::Mesh({0, 0, 0}, {1, 1, 1}, cubeMesh, {});

                    const float3 forward{
                        Cos(pitch) * Sin(yaw),
                        Sin(pitch),
                        Cos(pitch) * Cos(yaw),
                    };

                    static float3 cameraPos{0.f, 0.f, 5.f};

                    const float3 worldUp = {0.0f, 1.0f, 0.0f};
                    const float3 right = Vec::Normalized(Vec::Cross(worldUp, forward));
                    const float3 up = Vec::Normalized(Vec::Cross(forward, right));
                    const float moveStep = moveSpeed * frame.dt;

                    cameraPos += forward * movement[1] * moveStep;
                    cameraPos += right * movement[0] * moveStep;
                    cameraPos += worldUp * verticalInput * moveStep;

                    const float3 targetPos = cameraPos + forward;

                    Renderer::SetLookAtView(cameraPos, targetPos, worldUp);

                    Renderer::SetPerspectiveProjection(rtInfo.width, rtInfo.height, fovDeg, .01f, 1000.f);

                    Renderer::CmdFlush(frame.cmd);
                    DebugTextRenderer::CmdFlush(frame.cmd);

#if !defined(NDEBUG)
                    {
                        span<byteview> lines = DevLog::Snapshot(alloc, 8);
                        if (lines.size > 0)
                        {
                            CellRenderer::Begin(8, 8, 100, (uint32_t)lines.size);
                            for (uint64_t i = 0; i < lines.size; ++i)
                                CellRenderer::Text(0, (uint32_t)i, lines[i], 0xFFFF8080u, 0xFF000000u);
                            CellRenderer::CmdFlush(frame.cmd);
                        }
                        Profiler::CmdFlush(frame.cmd, 8, 8 + 32 * 9, frame.fps);
                        Tunables::CmdFlush(frame.cmd, 8, 8 + 32 * 18);
                    }
#endif
                }
                Rhi::PassEnd();
            }

            rhi_texture renderTarget = Rhi::GetTexture(rtv);

            Rhi::CmdTransitionTexture(frame.cmd, renderTarget, rhi_texture_state::TransferSrc);
            Rhi::CmdTransitionTexture(frame.cmd, backbuffer, rhi_texture_state::TransferDst);

            Rhi::CmdCopyTexture(frame.cmd, backbuffer, renderTarget);

            Rhi::CmdTransitionTexture(frame.cmd, backbuffer, rhi_texture_state::Present);
        }

        Engine::FrameEnd(alloc);
    }
}

} // namespace nyla
