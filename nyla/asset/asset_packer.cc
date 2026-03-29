#include <cstdint>

#include "nyla/asset/asset.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/entrypoint.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/region_alloc.h"

namespace nyla
{

__declspec(dllexport) auto PlatformMain() -> int
{
    Platform::Init({});

    RegionAlloc rootAlloc;
    rootAlloc.Init(nullptr, 64_GiB, true);

    auto args = rootAlloc.PushArr<Str>(256);
    Platform::ParseStdArgs(args.Data(), args.Size());

    if (args.Size() < 2)
    {
        NYLA_LOG("usage: %s [output] {[input] [processor config]}", args[0]);
        return 1;
    }

    NYLA_LOG("Writing into %s", args[2]);

    const FileHandle outputFile =
        Platform::FileOpen(rootAlloc.PushPath(Str{args[2]}), FileOpenMode::Append | FileOpenMode::Write);
    NYLA_ASSERT(Platform::FileValid(outputFile));

    auto scratch = rootAlloc.PushSubAlloc(1024);

    for (uint32_t i = 1; i < args.Size(); ++i)
    {
        auto arg = Str{args[i]};
        scratch.PushCopyStr(Str{" \""});
        scratch.PushCopyStr(arg);
        scratch.PushCopyStr(Str{"\""});
    }

    Platform::FileWrite(outputFile, AssetPackerHeader{.count = scratch.GetBytesUsed()});
    Platform::FileWriteSpan(outputFile, Span{scratch.GetBase(), scratch.GetBytesUsed()});

    rootAlloc.Pop(scratch.GetBase());

    for (uint32_t i = 2; i < args.Size();)
    {
        auto dataPath = Str{args[i++]};
        auto processorConfig = Str{args[i++]};

        // NYLA_LOG("Packing " NYLA_SV_FMT " using config " NYLA_SV_FMT, NYLA_SV_ARG(dataPath),
        //          NYLA_SV_ARG(processorConfig));
    }

    return 0;
}

} // namespace nyla