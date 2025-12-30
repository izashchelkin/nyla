#include "nyla/platform/platform.h"
#include <cmath>
#include <cstdint>

namespace nyla
{

#if 0
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

    for (auto &change : PlatformFsGetFileChanges())
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
        }
    }

    if (srcChanged)
    {
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
#endif

void UpdateDtFps(uint64_t nowUs, uint32_t &fps, float &dt)
{
    static uint64_t lastUs = nowUs;
    static uint64_t dtUsAccum = 0;
    static uint32_t numFramesCounted = 0;

    const uint64_t dtUs = nowUs - lastUs;
    lastUs = nowUs;

    dt = static_cast<float>(dtUs) * 1e-6f;

    dtUsAccum += dtUs;
    ++numFramesCounted;

    constexpr uint64_t kSecondInUs = 1'000'000ull;
    if (dtUsAccum >= kSecondInUs / 2)
    {
        const double seconds = static_cast<double>(dtUsAccum) / 1'000'000.0;
        const double fpsF64 = numFramesCounted / seconds;

        fps = std::lround(fpsF64);

        dtUsAccum = 0;
        numFramesCounted = 0;
    }
}

} // namespace nyla