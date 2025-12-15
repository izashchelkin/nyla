#include "nyla/rhi/rhi_pipeline.h"

#include "absl/log/check.h"

namespace nyla
{

auto RhiGetVertexFormatSize(RhiVertexFormat format) -> uint32_t
{
    switch (format)
    {
    case nyla::RhiVertexFormat::None:
        break;

    case RhiVertexFormat::R32G32B32A32Float:
        return 16;
    }
    CHECK(false);
    return 0;
}

} // namespace nyla