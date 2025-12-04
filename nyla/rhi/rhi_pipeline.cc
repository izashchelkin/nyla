#include "nyla/rhi/rhi_pipeline.h"

namespace nyla
{

uint32_t RhiGetVertexFormatSize(RhiVertexFormat format)
{
    switch (format)
    {
    case nyla::RhiVertexFormat::None:
        break;

    case RhiVertexFormat::R32G32B32A32_Float:
        return 16;
    }
    CHECK(false);
    return 0;
}

} // namespace nyla