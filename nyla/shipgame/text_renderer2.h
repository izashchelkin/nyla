#include <string_view>

#include "nyla/shipgame/simple_graphics_pipeline.h"

namespace nyla {

extern SimplePipeline textrender2_pipeline;

void TextRenderer2Line(int32_t x, int32_t y, std::string_view text);

}  // namespace nyla