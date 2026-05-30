#include "nyla/commons/audio_backend.h"
#include "nyla/commons/thread.h"

#include "nyla/commons/fmt.h"
#include "nyla/commons/intrin.h"
#include "nyla/commons/macros.h"
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
    audio_backend_callback callback;
    void *userdata;
    uint32_t sampleRate;
    uint32_t channels;
    thread *thrd;
    uint32_t running;
    int16_t scratch[kMaxFramesPerWrite * kMaxChannels];
};
platform_audio *backend;

void FeederMain(void *)
{
    while (AtomicLoad32(&backend->running))
    {
        snd_pcm_sframes_t avail = snd_pcm_avail(backend->pcm);
        if (avail < 0)
        {
            int rc = snd_pcm_recover(backend->pcm, (int)avail, 1);
            if (rc < 0)
                break;
            continue;
        }
        if (avail == 0)
        {
            int rc = snd_pcm_wait(backend->pcm, 100);
            if (rc < 0)
            {
                if (snd_pcm_recover(backend->pcm, rc, 1) < 0)
                    break;
            }
            continue;
        }
        uint32_t want = (uint32_t)avail;
        if (want > kMaxFramesPerWrite)
            want = kMaxFramesPerWrite;
        backend->callback(backend->userdata, backend->scratch, want);

        const uint8_t *p = (const uint8_t *)backend->scratch;
        uint32_t framesLeft = want;
        const uint32_t frameSize = backend->channels * sizeof(int16_t);
        while (framesLeft > 0 && AtomicLoad32(&backend->running))
        {
            snd_pcm_sframes_t written = snd_pcm_writei(backend->pcm, p, framesLeft);
            if (written < 0)
            {
                if (snd_pcm_recover(backend->pcm, (int)written, 1) < 0)
                    return;
                break;
            }
            framesLeft -= (uint32_t)written;
            p += (size_t)written * frameSize;
        }
    }
}

} // namespace

namespace AudioBackend
{

void API Init(const audio_backend_settings &desc)
{
    ASSERT(!backend);
    ASSERT(desc.channels <= kMaxChannels);
    ASSERT(desc.callback);

    backend = &RegionAlloc::Alloc<platform_audio>(RegionAlloc::g_BootstrapAlloc);
    backend->callback = desc.callback;
    backend->userdata = desc.userdata;
    backend->sampleRate = desc.sampleRate;
    backend->channels = desc.channels;

    int res = snd_pcm_open(&backend->pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (res != 0)
    {
        LOG("snd_pcm_open: %s", snd_strerror(res));
        ASSERT(false);
    }

    res = snd_pcm_set_params(backend->pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, desc.channels,
                             desc.sampleRate, 1, desc.latencyUs);
    if (res != 0)
    {
        LOG("snd_pcm_set_params: %s", snd_strerror(res));
        ASSERT(false);
    }

    AtomicStore32(&backend->running, 1);
    backend->thrd = Thread::Create(RegionAlloc::g_BootstrapAlloc, &FeederMain, nullptr);
    Thread::SetName(*backend->thrd, "nyla-audio");
}

void API Destroy()
{
    if (!backend)
        return;
    AtomicStore32(&backend->running, 0);
    if (backend->pcm)
        snd_pcm_drop(backend->pcm);
    if (backend->thrd)
        Thread::Join(*backend->thrd);
    if (backend->pcm)
    {
        snd_pcm_close(backend->pcm);
        backend->pcm = nullptr;
    }
}

auto API GetSampleRate() -> uint32_t
{
    return backend->sampleRate;
}
auto API GetChannels() -> uint32_t
{
    return backend->channels;
}

} // namespace AudioBackend

} // namespace nyla
