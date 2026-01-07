#include "nyla/spirview/spirview.h"

#include "nyla/commons/assert.h"
#include "nyla/commons/log.h"
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

    std::vector<std::byte> spirvBytes = ReadFile("nyla/shaders/build/renderer2d.vs.hlsl.spv");
    if (spirvBytes.size() % 4)
    {
        NYLA_LOG("invalid spirv");
        return 1;
    }

    std::span<const uint32_t> spirvWords = {reinterpret_cast<uint32_t *>(spirvBytes.data()), spirvBytes.size() / 4};

    SpirviewReflectResult result{};
    NYLA_ASSERT(SpirviewReflect(spirvWords, &result));

    for (const SpirviewReflectResult::IdLocation &idLocation : result.locations)
    {
        NYLA_LOG("ID: %d, location %d", idLocation.id, idLocation.location);
    }

    for (const SpirviewReflectResult::IdSemantic &idSemantic : result.semantics)
    {
        NYLA_LOG("ID: %d, semantic %s", idSemantic.id, idSemantic.semantic.CString());
    }

    return 0;
}

} // namespace nyla

auto main() -> int
{
    return nyla::Main();
}