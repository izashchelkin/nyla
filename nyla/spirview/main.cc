#include "nyla/spirview/spirview.h"

#include "nyla/commons/logging/init.h"
#include "nyla/commons/os/readfile.h"
#include "nyla/spirview/spirview.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace nyla
{
auto Main() -> int
{
    LoggingInit();

    std::vector<std::byte> spirvBytes = ReadFile("nyla/apps/breakout/shaders/build/world.vs.hlsl.spv");
    if (spirvBytes.size() % 4)
    {
        NYLA_LOG("invalid spirv");
        return 1;
    }

    std::span<const uint32_t> spirvWords = {reinterpret_cast<uint32_t *>(spirvBytes.data()), spirvBytes.size() / 4};

    SpirviewReflectResult result{};
    NYLA_ASSERT(SpirviewReflect(spirvWords, &result));

    for (uint32_t i = 0; i < result.resourcesCount; ++i)
    {
        const auto &resource = std::get<SpirviewResource>(result.records[result.resources[i]]);
    }

    return 0;
}

} // namespace nyla

auto main() -> int
{
    return nyla::Main();
}