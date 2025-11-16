#include <string_view>

#include "nyla/vulkan/simple_graphics_pipeline.h"

namespace nyla {

extern Sgp gamerenderer2_pipeline;
extern Sgp gridrenderer2_pipeline;
extern Sgp textrender2_pipeline;

void GridRenderer2Render();
void TextRenderer2Line(int32_t x, int32_t y, std::string_view text);

}  // namespace nyla