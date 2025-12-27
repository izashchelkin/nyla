#include "nyla/audio/wave.h"
#include "nyla/commons/os/readfile.h"
#include "nyla/platform/platform_audio.h"

#include <alsa/asoundlib.h>
#include <span>

namespace nyla
{

auto Main() -> int
{
    std::vector<std::byte> bytes =
        ReadFile("assets/BreakoutRechargedPaddleAndBall/Ball bounce off the player paddle.wav");

    ParseWavFileResult wav = ParseWavFile(bytes);

    g_PlatformAudio->Init({
        .sampleRate = wav.GetSampleRate(),
        .channels = wav.GetNumChannels(),
    });

    while (true)
    {
        g_PlatformAudio->Write(wav.data);
    }

    g_PlatformAudio->Destroy();

#if 0
    snd_pcm_t *pcm;
    CHECK_EQ(snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0), 0);

    snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, wav.GetNumChannels(),
                       wav.GetSampleRate(), 1, 500000);

    const signed short *samples = wav.data.data();
    auto framesLeft = (snd_pcm_sframes_t)(wav.data.size() / (wav.fmt->bitsPerSample / 8) / wav.GetNumChannels());

    while (framesLeft > 0)
    {
        LOG(INFO) << "left: " << framesLeft;

        int numWritten = snd_pcm_writei(pcm, samples, framesLeft);
        if (numWritten == -EAGAIN)
            continue;

        CHECK_GE(numWritten, 0) << snd_strerror(numWritten);

        samples += numWritten * wav.GetNumChannels();
        framesLeft -= numWritten;
    }
#endif

    return 0;
}

} // namespace nyla

auto main() -> int
{
    return nyla::Main();
}