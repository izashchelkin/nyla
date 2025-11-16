#include <string_view>

#include "nyla/vulkan/simple_graphics_pipeline.h"

namespace nyla {

extern RenderPipeline textrender2_pipeline;

void TextRenderer2Line(int32_t x, int32_t y, std::string_view text);

}  // namespace nyla