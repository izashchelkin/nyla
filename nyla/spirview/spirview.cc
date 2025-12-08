#include "absl/log/check.h"
#include "absl/log/log.h"
#include "nyla/commons/os/readfile.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace nyla
{

namespace spirview_internal
{
};

constexpr uint32_t kSpirvMagic = 0x07230203;

void DecodeSpirvWord()
{
}

void Reflect(std::span<const std::byte> spirvBytes)
{
    for (uint32_t i = 0; i < spirvBytes.size(); i += 4)
    {
        uint32_t word = 0;
        word |= (static_cast<uint32_t>(spirvBytes[i + 0]) << 0);
        word |= (static_cast<uint32_t>(spirvBytes[i + 1]) << 8);
        word |= (static_cast<uint32_t>(spirvBytes[i + 2]) << 16);
        word |= (static_cast<uint32_t>(spirvBytes[i + 3]) << 24);

        LOG(INFO) << std::hex << word << std::dec;
    }
}

namespace
{

auto Main() -> int
{
    std::vector<std::byte> spirvBytes = ReadFile("nyla/apps/breakout/shaders/build/world.vs.hlsl.spv");
    Reflect(spirvBytes);

    return 0;
}

} // namespace

} // namespace nyla

auto main() -> int
{
    return nyla::Main();
}