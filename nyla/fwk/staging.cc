#include "absl/log/log.h"
#include "nyla/commons/os/clock.h"
#include "nyla/platform/platform.h"

namespace nyla
{

auto RecompileShadersIfNeeded() -> bool
{
    static bool b = false;
    if (!b)
    {
        b = true;
        return true;
    }

    return false;

    static bool spvChanged = true;
    static bool srcChanged = true;

    for (auto &change : PlatformFsGetChanges())
    {
        if (change.seen)
            continue;
        change.seen = true;

        const auto &path = change.path;
        if (path.ends_with(".spv"))
        {
            spvChanged = true;
        }
        else if (path.ends_with(".hlsl"))
        {
            srcChanged = true;
        }
        else
        {
            LOG(INFO) << "ignoring " << path;
        }
    }

    if (srcChanged)
    {
        LOG(INFO) << "shaders recompiling";
        system("python3 /home/izashchelkin/nyla/scripts/shaders.py");
        usleep(1e6);
        PlatformProcessEvents();

        srcChanged = false;
    }

    if (srcChanged || spvChanged)
    {
        spvChanged = false;
        return true;
    }

    return false;
}

void UpdateDtFps(uint32_t &fps, float &dt)
{
    static uint64_t last = GetMonotonicTimeNanos();
    const uint64_t now = GetMonotonicTimeNanos();
    const uint64_t dtnanos = now - last;
    last = now;

    dt = dtnanos / 1e9;

    static float dtnanosaccum = .0f;
    dtnanosaccum += dtnanos;

    static uint32_t fpsFrames = 0;
    ++fpsFrames;

    if (dtnanosaccum >= .5f * 1e9)
    {
        fps = (1e9 / dtnanosaccum) * fpsFrames;

        fpsFrames = 0;
        dtnanosaccum = .0f;
    }
}

} // namespace nyla