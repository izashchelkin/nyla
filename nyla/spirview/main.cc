#include "nyla/commons/containers/inline_string.h"
#include "nyla/commons/containers/inline_vec.h"
#include "nyla/spirview/spirview.h"

#include "nyla/commons/assert.h"
#include "nyla/commons/log.h"
#include "nyla/commons/os/readfile.h"
#include "nyla/spirview/spirview.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

#define spv_enable_utility_code
#if defined(__linux__)
#include <spirv/unified1/spirv.hpp>
#else
#include <spirv-headers/spirv.hpp>
#endif

namespace nyla
{
auto Main() -> int
{
    std::vector<std::byte> spirvBytes = ReadFile("nyla/shaders/build/renderer2d.vs.hlsl.spv");
    if (spirvBytes.size() % 4)
    {
        NYLA_LOG("invalid spirv");
        return 1;
    }


    NYLA_LOG("=====================");

    for (const IdLocation &idLocation : locations)
    {
        NYLA_LOG("ID: %d, location %d", idLocation.id, idLocation.location);
    }

    NYLA_LOG("=====================");

    for (const IdSemantic &idSemantic : semantics)
    {
        NYLA_LOG("ID: %d, semantic %s", idSemantic.id, idSemantic.semantic.CString());
    }

    NYLA_LOG("=====================");

    for (uint32_t id : outputs)
    {
        NYLA_LOG("ID: %d is output", id);
    }

    NYLA_LOG("=====================");

    for (uint32_t id : inputs)
    {
        NYLA_LOG("ID: %d is input", id);
    }

    NYLA_LOG("=====================");

    getc(stdin);

    return 0;
}

} // namespace nyla

auto main() -> int
{
    return nyla::Main();
}