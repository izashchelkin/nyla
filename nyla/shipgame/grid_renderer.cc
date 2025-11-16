#include "nyla/shipgame/grid_renderer.h"

#include "nyla/vulkan/render_pipeline.h"

namespace nyla {

void GridRender() {
  RpDraw(grid_pipeline, 3, {}, {});
}

RenderPipeline grid_pipeline{
    .Init =
        [](RenderPipeline& rp) {
          RpAttachVertShader(rp, "nyla/shipgame/shaders/build/grid_vert.spv");
          RpAttachFragShader(rp, "nyla/shipgame/shaders/build/grid_frag.spv");
        },
};

}  // namespace nyla