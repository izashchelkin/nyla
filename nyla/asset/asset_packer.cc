#include <cstdint>

#include "nyla/asset/asset.h"
#include "nyla/commons/bootstrap.h"
#include "nyla/commons/entrypoint.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_string.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/span.h"

namespace nyla
{

auto PlatformMain() -> int
{
    Platform::Init({});
    Bootstrap();

    region_alloc arena = RegionAlloc::Create(1_MiB, 0);

    span<byteview> args = RegionAlloc::AllocArray<byteview>(RegionAlloc::g_BootstrapAlloc, 256);
    Platform::ParseStdArgs(args.data, args.size);

    if (args.size < 2)
    {
        NYLA_LOG("usage: %s [output] {[input] [processor config]}", args[0]);
        return 1;
    }

    for (auto arg : args)
    {
        if (arg.size > 0)
            NYLA_LOG("" NYLA_SV_FMT, NYLA_SV_ARG(arg));
    }

    NYLA_LOG("Writing into " NYLA_SV_FMT, NYLA_SV_ARG(args[1]));

    const FileHandle outputFile =
        Platform::FileOpen(RegionAlloc::SafeCStr<64>(arena, args[1]), FileOpenMode::Append | FileOpenMode::Write);
    NYLA_ASSERT(Platform::FileValid(outputFile));

    RegionAlloc::Reset(arena);
    auto &cmdLineBuffer = RegionAlloc::AllocString<1_KiB>(arena);

    for (uint32_t i = 1; i < args.size; ++i)
    {
        InlineString::AppendSuffix(cmdLineBuffer, StringLiteralAsView(" \""));
        InlineString::AppendSuffix(cmdLineBuffer, args[i]);
        InlineString::AppendSuffix(cmdLineBuffer, StringLiteralAsView("\""));
    }

    Platform::FileWrite(outputFile, AssetPackerHeader{.count = (uint32_t)cmdLineBuffer.size});
    Platform::FileWriteSpan(outputFile, (byteview)cmdLineBuffer);

    RegionAlloc::Reset(arena);

    for (uint32_t i = 2; i < args.size;)
    {
        byteview dataPath = args[i++];
        byteview processorConfig = args[i++];

        // NYLA_LOG("Packing " NYLA_SV_FMT " using config " NYLA_SV_FMT, NYLA_SV_ARG(dataPath),
        //          NYLA_SV_ARG(processorConfig));
    }

    return 0;
}

} // namespace nyla