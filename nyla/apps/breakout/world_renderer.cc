// #include "nyla/apps/breakout/world_renderer.h"
// #include "nyla/commons/math/vec.h"
//
// #include "nyla/commons/math/mat.h"
// #include "nyla/commons/memory/charview.h"
// #include "nyla/engine0/render_pipeline.h"
// #include "nyla/rhi/rhi_pipeline.h"
// #include "nyla/rhi/rhi_texture.h"
//
// namespace nyla
// {
//
// namespace
// {
//
// struct SceneTransforms
// {
//     float4x4 vp;
//     float4x4 invVp;
// };
//
// struct DynamicUbo
// {
//     float4x4 model;
//     float3 color;
//     float pad;
// };
//
// } // namespace
//
// constexpr float kMetersOnScreen = 64.f;
//
// void WorldSetUp()
// {
//     float worldW;
//     float worldH;
//
//     const float base = kMetersOnScreen;
//
//     const RhiTextureInfo backbuffer = RhiGetTextureInfo(RhiGetBackbufferTexture());
//     const auto width = static_cast<float>(backbuffer.width);
//     const auto height = static_cast<float>(backbuffer.height);
//     const float aspect = width / height;
//
//     worldH = base;
//     worldW = base * aspect;
//
//     float4x4 view = float4x4::Identity();
//     float4x4 proj = float4x4::Ortho(-worldW * .5f, worldW * .5f, worldH * .5f, -worldH * .5f, 0.f, 1.f);
//
//     float4x4 vp = proj.Mult(view);
//     float4x4 invVp = vp.Inversed();
//     SceneTransforms scene = {
//         .vp = vp,
//         .invVp = invVp,
//     };
//
//     RpPushConst(worldPipeline, ByteViewPtr(&scene));
// }
//
// void WorldRender(float2 pos, float3 color, float width, float height, const RpMesh &mesh)
// {
//     DynamicUbo dynamicData{};
//     dynamicData.color = color;
//
//     dynamicData.model = float4x4::Translate(pos);
//     dynamicData.model = dynamicData.model.Mult(float4x4::Scale(float4{width, height, 1, 1}));
//
//     RpDraw(worldPipeline, mesh, ByteViewPtr(&dynamicData));
// }
//
// Rp worldPipeline{
//     .debugName = "World",
//     .dynamicUniform =
//         {
//             .enabled = true,
//             .size = 60000,
//             .range = sizeof(DynamicUbo),
//         },
//     .vertBuf =
//         {
//             .enabled = true,
//             .size = 1 << 22,
//             .attrs =
//                 {
//                     RhiVertexFormat::R32G32B32A32Float,
//                 },
//         },
//     .init = [](Rp &rp) -> void {
//         RpAttachVertShader(rp, "nyla/apps/breakout/shaders/build/world.vs.hlsl.spv");
//         RpAttachFragShader(rp, "nyla/apps/breakout/shaders/build/world.ps.hlsl.spv");
//     },
// };
//
// } // namespace nyla