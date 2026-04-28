#include "nyla/commons/platform_audio.h"

#include "nyla/commons/fmt.h"
#include "nyla/commons/intrin.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/platform_thread.h"
#include "nyla/commons/region_alloc.h"

#include <alsa/asoundlib.h>
#include <cstdint>

namespace nyla
{

namespace
{

constexpr uint32_t kMaxFramesPerWrite = 512;
constexpr uint32_t kMaxChannels = 8;

struct platform_audio
{
    snd_pcm_t *pcm;
    PlatformAudioCallback callback;
    void *user;
    uint32_t sampleRate;
    uint32_t channels;
    platform_thread *thread;
    uint32_t running;
    int16_t scratch[kMaxFramesPerWrite * kMaxChannels];
};
platform_audio *audio;

void FeederMain(void *)
{
    while (AtomicLoad32(&audio->running))
    {
        snd_pcm_sframes_t avail = snd_pcm_avail(audio->pcm);
        if (avail < 0)
        {
            int rc = snd_pcm_recover(audio->pcm, (int)avail, 1);
            if (rc < 0)
                break;
            continue;
        }

        if (avail == 0)
        {
            int rc = snd_pcm_wait(audio->pcm, 100);
            if (rc < 0)
            {
                if (snd_pcm_recover(audio->pcm, rc, 1) < 0)
                    break;
            }
            continue;
        }

        uint32_t want = (uint32_t)avail;
        if (want > kMaxFramesPerWrite)
            want = kMaxFramesPerWrite;

        audio->callback(audio->user, audio->scratch, want);

        const uint8_t *p = (const uint8_t *)audio->scratch;
        uint32_t framesLeft = want;
        const uint32_t frameSize = audio->channels * sizeof(int16_t);

        while (framesLeft > 0 && AtomicLoad32(&audio->running))
        {
            snd_pcm_sframes_t written = snd_pcm_writei(audio->pcm, p, framesLeft);
            if (written < 0)
            {
                if (snd_pcm_recover(audio->pcm, (int)written, 1) < 0)
                    return;
                break;
            }
            framesLeft -= (uint32_t)written;
            p += (size_t)written * frameSize;
        }
    }
}

} // namespace

namespace PlatformAudio
{

void API Init(const PlatformAudioInitDesc &desc)
{
    ASSERT(!audio);
    ASSERT(desc.channels <= kMaxChannels);
    ASSERT(desc.callback);

    audio = &RegionAlloc::Alloc<platform_audio>(RegionAlloc::g_BootstrapAlloc);
    audio->callback = desc.callback;
    audio->user = desc.user;
    audio->sampleRate = desc.sampleRate;
    audio->channels = desc.channels;

    int res = snd_pcm_open(&audio->pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (res != 0)
    {
        LOG("snd_pcm_open: %s", snd_strerror(res));
        ASSERT(false);
    }

    res = snd_pcm_set_params(audio->pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, desc.channels,
                             desc.sampleRate, 1, desc.latencyUs);
    if (res != 0)
    {
        LOG("snd_pcm_set_params: %s", snd_strerror(res));
        ASSERT(false);
    }

    AtomicStore32(&audio->running, 1);
    audio->thread = PlatformThread::Create(RegionAlloc::g_BootstrapAlloc, &FeederMain, nullptr);
    PlatformThread::SetName(*audio->thread, "nyla-audio");
}

void API Destroy()
{
    if (!audio)
        return;

    AtomicStore32(&audio->running, 0);

    if (audio->pcm)
        snd_pcm_drop(audio->pcm);

    if (audio->thread)
        PlatformThread::Join(*audio->thread);

    if (audio->pcm)
    {
        snd_pcm_close(audio->pcm);
        audio->pcm = nullptr;
    }
}

auto API GetSampleRate() -> uint32_t
{
    return audio->sampleRate;
}

auto API GetChannels() -> uint32_t
{
    return audio->channels;
}

} // namespace PlatformAudio

} // namespace nyla
