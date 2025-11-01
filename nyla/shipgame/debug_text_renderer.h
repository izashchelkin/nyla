#include <string_view>

#include "nyla/vulkan/vulkan.h"

namespace nyla {

void InitDebugTextRenderer();

void DebugRenderTextBegin(const Vulkan_FrameData& frame_data, int32_t x,
                          int32_t y, std::string_view text);
void DebugRenderText(const Vulkan_FrameData& frame_data);

}  // namespace nyla
