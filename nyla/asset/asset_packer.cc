#include <cstdint>

#include "nyla/asset/asset.h"
#include "nyla/commons/entrypoint.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/path.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/region_alloc.h"

namespace nyla
{

auto PlatformMain() -> int
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

    for (auto arg : args)
    {
        if (!arg.Empty())
            NYLA_LOG("" NYLA_SV_FMT, NYLA_SV_ARG(arg));
    }

    NYLA_LOG("Writing into " NYLA_SV_FMT, NYLA_SV_ARG(args[1]));

    auto scratch = rootAlloc.PushSubAlloc(1 << 20);
    const FileHandle outputFile =
        Platform::FileOpen(CreatePath(scratch, args[1]).CStr(), FileOpenMode::Append | FileOpenMode::Write);
    NYLA_ASSERT(Platform::FileValid(outputFile));

    scratch.Reset();

    for (uint32_t i = 1; i < args.Size(); ++i)
    {
        Str arg = args[i];
        scratch.PushCopyStr(AsStr(" \""));
        scratch.PushCopyStr(arg);
        scratch.PushCopyStr(AsStr("\""));
    }

    Platform::FileWrite(outputFile, AssetPackerHeader{.count = scratch.GetBytesUsed()});
    Platform::FileWriteSpan(outputFile, Span{scratch.GetBase(), scratch.GetBytesUsed()});

    rootAlloc.Pop(scratch.GetBase());

    for (uint32_t i = 2; i < args.Size();)
    {
        Str dataPath = args[i++];
        Str processorConfig = args[i++];

        // NYLA_LOG("Packing " NYLA_SV_FMT " using config " NYLA_SV_FMT, NYLA_SV_ARG(dataPath),
        //          NYLA_SV_ARG(processorConfig));
    }

    return 0;
}

} // namespace nyla