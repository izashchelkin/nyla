#include <cstdint>

#include "nyla/alloc/region_alloc.h"
#include "nyla/asset/asset.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/log.h"
#include "nyla/platform/platform.h"

namespace nyla
{

auto PlatformMain(std::span<const char *> argv) -> int
{
    if (argv.size() < 2)
    {
        NYLA_LOG("usage: %s [output] {[input] [processor config]}", argv[0]);
        return 1;
    }

    Platform::Init({});

    RegionAlloc rootAlloc;
    rootAlloc.Init(nullptr, 64_GiB, true);

    NYLA_LOG("Writing into %s", argv[2]);

    const FileHandle outputFile =
        Platform::FileOpen(rootAlloc.PushPath(Str{argv[2]}), FileOpenMode::Append | FileOpenMode::Write);
    NYLA_ASSERT(Platform::FileValid(outputFile));

    auto scratch = rootAlloc.PushSubAlloc(1024);

    for (uint32_t i = 1; i < argv.size(); ++i)
    {
        auto arg = Str{argv[i]};
        scratch.PushCopyStr(Str{" \""});
        scratch.PushCopyStr(arg);
        scratch.PushCopyStr(Str{"\""});
    }

    Platform::FileWrite(outputFile, AssetPackerHeader{.count = scratch.GetBytesUsed()});
    Platform::FileWriteSpan(outputFile, std::span{scratch.GetBase(), scratch.GetBytesUsed()});

    rootAlloc.Pop(scratch.GetBase());

    for (uint32_t i = 2; i < argv.size();)
    {
        std::string_view dataPath = argv[i++];
        std::string_view processorConfig = argv[i++];

        NYLA_LOG("Packing " NYLA_SV_FMT " using config " NYLA_SV_FMT, NYLA_SV_ARG(dataPath),
                 NYLA_SV_ARG(processorConfig));
    }

    return 0;
}

} // namespace nyla