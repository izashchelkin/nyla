#pragma once

#include <cstdint>

#include "nyla/commons/macros.h"

namespace nyla
{

using audio_backend_callback = void (*)(void *userdata, int16_t *out, uint32_t numFrames);

struct audio_backend_settings
{
    uint32_t sampleRate;
    uint32_t channels;
    uint32_t latencyUs;

    audio_backend_callback callback;
    void *userdata;
};

namespace AudioBackend
{

void API Init(const audio_backend_settings &desc);
void API Destroy();
auto API GetSampleRate() -> uint32_t;
auto API GetChannels() -> uint32_t;

} // namespace AudioBackend

} // namespace nyla