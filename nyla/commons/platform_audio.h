#pragma once

#include <cstdint>

#include "nyla/commons/macros.h"

namespace nyla
{

using PlatformAudioCallback = void (*)(void *user, int16_t *out, uint32_t numFrames);

struct PlatformAudioInitDesc
{
    uint32_t sampleRate;
    uint32_t channels;
    uint32_t latencyUs;
    PlatformAudioCallback callback;
    void *user;
};

namespace PlatformAudio
{

void API Init(const PlatformAudioInitDesc &desc);
void API Destroy();
auto API GetSampleRate() -> uint32_t;
auto API GetChannels() -> uint32_t;

} // namespace PlatformAudio

} // namespace nyla
