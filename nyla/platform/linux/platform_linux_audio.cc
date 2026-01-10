#include "nyla/platform/platform_audio.h"
#include <alsa/asoundlib.h>
#include <cerrno>
#include <cstdint>

namespace nyla
{

class PlatformAudio::Impl
{
  public:
    void Init(const PlatformAudioInitDesc &);
    void Destroy();
    void Write(std::span<const std::byte> data);

  private:
    uint32_t m_SampleRate{};
    uint32_t m_Channels{};
    uint32_t m_BytesPerChannel{};

    snd_pcm_t *m_Pcm{};
};

void PlatformAudio::Impl::Init(const PlatformAudioInitDesc &desc)
{
    int res = snd_pcm_open(&m_Pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (res != 0)
    {
        NYLA_LOG(snd_strerror(res));
        NYLA_ASSERT(false);
    }

    m_SampleRate = desc.sampleRate;
    m_Channels = desc.channels;
    m_BytesPerChannel = 2;

    const bool softResample = true; // TODO:
    snd_pcm_set_params(m_Pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, m_Channels, m_SampleRate,
                       softResample, desc.latencyUs);
}

void PlatformAudio::Impl::Destroy()
{
    if (m_Pcm)
    {
        snd_pcm_drain(m_Pcm);
        snd_pcm_close(m_Pcm);
    }
}

void PlatformAudio::Impl::Write(std::span<const std::byte> data)
{
    const uint32_t frameSize = m_Channels * m_BytesPerChannel;
    NYLA_ASSERT(data.size() % frameSize == 0);

    auto *p = data.data();

    uint32_t framesLeft = data.size() / frameSize;
    while (framesLeft > 0)
    {
        snd_pcm_sframes_t res = snd_pcm_writei(m_Pcm, p, framesLeft);
        if (res < 0)
        {
            if (res == -EAGAIN)
                snd_pcm_wait(m_Pcm, 1000);
            else
                snd_pcm_recover(m_Pcm, (int)res, 0);
        }
        else
        {
            framesLeft -= res;
            p += res * frameSize;
        }
    }
}

//

void PlatformAudio::Init(const PlatformAudioInitDesc &desc)
{
    NYLA_ASSERT(!m_Impl);
    m_Impl = new Impl{};
    m_Impl->Init(desc);
}

void PlatformAudio::Destroy()
{
    if (m_Impl)
    {
        m_Impl->Destroy();
        delete m_Impl;
    }
}

void PlatformAudio::Write(std::span<const std::byte> data)
{
    m_Impl->Write(data);
}

PlatformAudio *g_PlatformAudio = new PlatformAudio{};

} // namespace nyla