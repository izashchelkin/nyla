// #include "nyla/engine0/gui.h"
//
// #include <cstdint>
//
// #include "nyla/commons/math/vec.h"
// #include "nyla/commons/memory/charview.h"
// #include "nyla/engine0/render_pipeline.h"
// #include "nyla/rhi/rhi_pipeline.h"
//
// namespace nyla
// {
//
// namespace
// {
//
// struct StaticUbo
// {
//     float windowWidth;
//     float windowHeight;
// };
//
// }; // namespace
//
// void UiFrameBegin()
// {
//     const RhiTextureInfo backbuffer = RhiGetTextureInfo(RhiGetBackbufferTexture());
//     StaticUbo ubo{
//         .windowWidth = static_cast<float>(backbuffer.width),
//         .windowHeight = static_cast<float>(backbuffer.height),
//     };
//     RpStaticUniformCopy(guiPipeline, ByteViewPtr(&ubo));
// }
//
// static void UiBoxBegin(float x, float y, float width, float height)
// {
//     const float z = 0.f;
//     const float w = 0.f;
//
//     const RhiTextureInfo backbuffer = RhiGetTextureInfo(RhiGetBackbufferTexture());
//     if (x < 0.f)
//         x += static_cast<float>(backbuffer.width) - width;
//     if (y < 0.f)
//         y += static_cast<float>(backbuffer.height) - height;
//
//     std::array<float4, 6> rect = {
//         float4{x, y, z, w},
//         float4{x + width, y + height, z, w},
//         float4{x + width, y, z, w},
//         //
//         float4{x, y, z, w},
//         float4{x, y + height, z, w},
//         float4{x + width, y + height, z, w},
//     };
//
//     RpMesh mesh = RpVertCopy(guiPipeline, std::size(rect), ByteViewSpan(std::span{rect}));
//     RpDraw(guiPipeline, mesh, {});
// }
//
// void UiBoxBegin(int32_t x, int32_t y, uint32_t width, uint32_t height)
// {
//     UiBoxBegin((float)x, (float)y, (float)width, (float)height);
// }
//
// void UiText(std::string_view text)
// {
//     //
// }
//
// Rp guiPipeline{
//     .debugName = "GUI",
//     .disableCulling = true,
//     .staticUniform =
//         {
//             .enabled = true,
//             .size = sizeof(StaticUbo),
//             .range = sizeof(StaticUbo),
//         },
//     .vertBuf =
//         {
//             .enabled = true,
//             .size = 1 << 20,
//             .attrs =
//                 {
//                     RhiVertexFormat::R32G32B32A32Float,
//                 },
//         },
//     .init = [](Rp &rp) -> void {
//         RpAttachVertShader(rp, "/home/izashchelkin/nyla/nyla/engine0/shaders/build/gui.vs.hlsl.spv");
//         RpAttachFragShader(rp, "/home/izashchelkin/nyla/nyla/engine0/shaders/build/gui.ps.hlsl.spv");
//     },
// };
//
// } // namespace nyla