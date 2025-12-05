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

    static bool spv_changed = true;
    static bool src_changed = true;

    for (auto &change : PlatformFsGetChanges())
    {
        if (change.seen)
            continue;
        change.seen = true;

        const auto &path = change.path;
        if (path.ends_with(".spv"))
        {
            spv_changed = true;
        }
        else if (path.ends_with(".hlsl"))
        {
            src_changed = true;
        }
        else
        {
            LOG(INFO) << "ignoring " << path;
        }
    }

    if (src_changed)
    {
        LOG(INFO) << "shaders recompiling";
        system("python3 /home/izashchelkin/nyla/scripts/shaders.py");
        usleep(1e6);
        PlatformProcessEvents();

        src_changed = false;
    }

    if (src_changed || spv_changed)
    {
        spv_changed = false;
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

    static uint32_t fps_frames = 0;
    ++fps_frames;

    if (dtnanosaccum >= .5f * 1e9)
    {
        fps = (1e9 / dtnanosaccum) * fps_frames;

        fps_frames = 0;
        dtnanosaccum = .0f;
    }
}

} // namespace nyla