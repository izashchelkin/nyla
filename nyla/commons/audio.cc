#include "nyla/commons/audio.h"

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/asset_manager.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/platform_audio.h"
#include "nyla/commons/platform_mutex.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/wave.h"

#include <cinttypes>
#include <cstdint>

namespace nyla
{

namespace
{

struct voice_data
{
    const int16_t *samples;
    uint32_t numFrames;
    uint32_t sampleRate;
    uint16_t channels;
    uint32_t cursor;
    float volume;
    bool loop;
};

struct clip_slot
{
    uint64_t guid;
    audio_clip clip;
};

struct audio_state
{
    handle_pool<voice, voice_data, 64> voices;
    handle_pool<audio_clip_handle, clip_slot, 64> clips;
    platform_mutex *mutex;
    uint32_t deviceSampleRate;
    uint32_t deviceChannels;
    float masterVolume;
};
audio_state *audio;

void MixCallback(void *, int16_t *out, uint32_t numFrames)
{
    PlatformMutex::Lock(*audio->mutex);

    const uint32_t outCh = audio->deviceChannels;
    const float master = audio->masterVolume;

    for (uint32_t i = 0; i < numFrames * outCh; ++i)
        out[i] = 0;

    for (uint32_t i = 0; i < HandlePool::Capacity(audio->voices); ++i)
    {
        auto &slot = audio->voices[i];
        if (!slot.used)
            continue;

        voice_data &v = slot.data;
        uint32_t framesProduced = 0;

        while (framesProduced < numFrames)
        {
            if (v.cursor >= v.numFrames)
            {
                if (v.loop)
                {
                    v.cursor = 0;
                }
                else
                {
                    HandlePool::Free(slot);
                    break;
                }
            }

            uint32_t take = numFrames - framesProduced;
            uint32_t left = v.numFrames - v.cursor;
            if (take > left)
                take = left;

            const float gain = v.volume * master;

            for (uint32_t f = 0; f < take; ++f)
            {
                for (uint32_t c = 0; c < outCh; ++c)
                {
                    uint32_t srcCh;
                    if (v.channels == 1)
                        srcCh = 0;
                    else if (c < v.channels)
                        srcCh = c;
                    else
                        srcCh = v.channels - 1;

                    int32_t s = (int32_t)v.samples[(v.cursor + f) * v.channels + srcCh];
                    int32_t scaled = (int32_t)((float)s * gain);
                    int32_t sum = (int32_t)out[(framesProduced + f) * outCh + c] + scaled;
                    if (sum > 32767)
                        sum = 32767;
                    if (sum < -32768)
                        sum = -32768;
                    out[(framesProduced + f) * outCh + c] = (int16_t)sum;
                }
            }

            v.cursor += take;
            framesProduced += take;
        }
    }

    PlatformMutex::Unlock(*audio->mutex);
}

} // namespace

namespace Audio
{

void API Bootstrap(uint32_t sampleRate, uint32_t channels, uint32_t latencyUs)
{
    ASSERT(!audio);

    audio = &RegionAlloc::Alloc<audio_state>(RegionAlloc::g_BootstrapAlloc);
    audio->mutex = PlatformMutex::Create(RegionAlloc::g_BootstrapAlloc);
    audio->deviceSampleRate = sampleRate;
    audio->deviceChannels = channels;
    audio->masterVolume = 1.f;

    PlatformAudio::Init({
        .sampleRate = sampleRate,
        .channels = channels,
        .latencyUs = latencyUs,
        .callback = &MixCallback,
        .user = nullptr,
    });

    AssetManager::Subscribe(
        [](uint64_t guid, byteview, void *) {
            for (uint32_t i = 0; i < HandlePool::Capacity(audio->clips); ++i)
            {
                auto &slot = audio->clips[i];
                if (!slot.used)
                    continue;
                if (slot.data.guid != guid)
                    continue;

                slot.data.clip = LoadWav(AssetManager::Get(guid));
                LOG("audio: reloaded clip 0x%016" PRIx64, guid);
            }
        },
        nullptr);
}

void API Shutdown()
{
    if (!audio)
        return;

    PlatformAudio::Destroy();
    PlatformMutex::Destroy(*audio->mutex);
}

auto API LoadWav(byteview wavBlob) -> audio_clip
{
    ParseWavFileResult parsed = ParseWavFile(wavBlob);

    audio_clip clip{};
    clip.samples = (const int16_t *)parsed.data.data;
    clip.numFrames = (uint32_t)(parsed.data.size / (parsed.fmt->numChannels * sizeof(int16_t)));
    clip.sampleRate = parsed.fmt->numSamplesPerSec;
    clip.channels = parsed.fmt->numChannels;
    return clip;
}

auto API Play(const audio_clip &clip, const AudioPlayDesc &desc) -> voice
{
    ASSERT(clip.sampleRate == audio->deviceSampleRate, "resampling not implemented");

    voice_data data{
        .samples = clip.samples,
        .numFrames = clip.numFrames,
        .sampleRate = clip.sampleRate,
        .channels = clip.channels,
        .cursor = 0,
        .volume = desc.volume,
        .loop = desc.loop,
    };

    PlatformMutex::Lock(*audio->mutex);
    voice v = HandlePool::Acquire(audio->voices, data);
    PlatformMutex::Unlock(*audio->mutex);
    return v;
}

auto API DeclareClip(uint64_t guid) -> audio_clip_handle
{
    audio_clip clip = LoadWav(AssetManager::Get(guid));
    return HandlePool::Acquire(audio->clips, clip_slot{.guid = guid, .clip = clip});
}

auto API ResolveClip(audio_clip_handle h) -> audio_clip
{
    return HandlePool::ResolveData(audio->clips, h).clip;
}

auto API Play(audio_clip_handle h, const AudioPlayDesc &desc) -> voice
{
    return Play(ResolveClip(h), desc);
}

void API Stop(voice v)
{
    PlatformMutex::Lock(*audio->mutex);
    handle_slot<voice_data> *slot;
    if (HandlePool::TryResolveSlot(audio->voices, v, slot))
        HandlePool::Free(*slot);
    PlatformMutex::Unlock(*audio->mutex);
}

void API SetVolume(voice v, float volume)
{
    PlatformMutex::Lock(*audio->mutex);
    handle_slot<voice_data> *slot;
    if (HandlePool::TryResolveSlot(audio->voices, v, slot))
        slot->data.volume = volume;
    PlatformMutex::Unlock(*audio->mutex);
}

auto API IsPlaying(voice v) -> bool
{
    PlatformMutex::Lock(*audio->mutex);
    handle_slot<voice_data> *slot;
    bool playing = HandlePool::TryResolveSlot(audio->voices, v, slot);
    PlatformMutex::Unlock(*audio->mutex);
    return playing;
}

void API SetMasterVolume(float volume)
{
    PlatformMutex::Lock(*audio->mutex);
    audio->masterVolume = volume;
    PlatformMutex::Unlock(*audio->mutex);
}

} // namespace Audio

} // namespace nyla
