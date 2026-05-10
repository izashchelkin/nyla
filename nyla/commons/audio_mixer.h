#pragma once

#include <cstdint>

#include "nyla/commons/handle.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

struct audio_clip
{
    const int16_t *samples;
    uint32_t numFrames;
    uint32_t sampleRate;
    uint16_t channels;
};

struct audio_clip_handle : handle
{
};

struct voice : handle
{
};

struct AudioPlayDesc
{
    float volume = 1.f;
    bool loop = false;
};

namespace Audio
{

void API Bootstrap(uint32_t sampleRate, uint32_t channels, uint32_t latencyUs);
void API Shutdown();

auto API LoadWav(byteview wavBlob) -> audio_clip;

auto API DeclareClip(uint64_t guid) -> audio_clip_handle;
auto API ResolveClip(audio_clip_handle clip) -> audio_clip;

auto API Play(const audio_clip &clip, const AudioPlayDesc &desc = {}) -> voice;
auto API Play(audio_clip_handle clip, const AudioPlayDesc &desc = {}) -> voice;
void API Stop(voice v);
void API SetVolume(voice v, float volume);
auto API IsPlaying(voice v) -> bool;

void API SetMasterVolume(float volume);

} // namespace Audio

} // namespace nyla
