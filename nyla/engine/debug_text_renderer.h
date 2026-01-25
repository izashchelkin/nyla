#pragma once

#include "nyla/commons/containers/inline_vec.h"
#include "nyla/commons/math/vec.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_pipeline.h"
#include <array>
#include <cstdint>
#include <string_view>

namespace nyla
{

class DebugTextRenderer
{
  public:
    void Init();
    void Text(int32_t x, int32_t y, std::string_view text);
    void Draw(RhiCmdList cmd);

  private:
    RhiGraphicsPipeline m_Pipeline;

    struct DrawData
    {
        std::array<uint4, 17> words;
        int32_t originX;
        int32_t originY;
        uint32_t wordCount;
        uint32_t pad;
        std::array<float, 4> fg;
        std::array<float, 4> bg;
    };
    InlineVec<DrawData, 4> m_PendingDraws;
};

} // namespace nyla