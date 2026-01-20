#include "nyla/formats/gltf/gltf_parser.h"
#include <cstdint>

namespace nyla
{

auto GltfParser::Parse(std::span<const std::uint32_t> data) -> bool
{
    if (data[0] != kMagic)
        return false;
}

} // namespace nyla