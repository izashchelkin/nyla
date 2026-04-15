#include "nyla/commons/sampler_manager.h"
#include "nyla/commons/rhi.h"

namespace nyla
{

namespace SamplerManager
{

void API Bootstrap()
{
    auto addSampler = [](sampler_type samplerType, rhi_filter filter, rhi_sampler_address_mode addressMode) -> void {
        rhi_sampler sampler = Rhi::CreateSampler({
            .minFilter = filter,
            .magFilter = filter,
            .addressModeU = addressMode,
            .addressModeV = addressMode,
            .addressModeW = addressMode,
        });
    };
    addSampler(sampler_type::LinearClamp, rhi_filter::Linear, rhi_sampler_address_mode::ClampToEdge);
    addSampler(sampler_type::LinearRepeat, rhi_filter::Linear, rhi_sampler_address_mode::Repeat);
    addSampler(sampler_type::NearestClamp, rhi_filter::Nearest, rhi_sampler_address_mode::ClampToEdge);
    addSampler(sampler_type::NearestRepeat, rhi_filter::Nearest, rhi_sampler_address_mode::Repeat);
}

} // namespace SamplerManager

} // namespace nyla
