#include "absl/log/check.h"
#include "absl/log/log.h"
#include "nyla/audio/wave.h"
#include "nyla/commons/os/readfile.h"

#include <alsa/asoundlib.h>

namespace nyla
{

auto Main() -> int
{
    std::vector<std::byte> bytes =
        ReadFile("assets/BreakoutRechargedPaddleAndBall/Ball bounce off the player paddle.wav");

    ParseWavFileResult wav = ParseWavFile(bytes);

    snd_pcm_t *pcm;
    CHECK_EQ(snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0), 0);

    snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, wav.GetNumChannels(),
                       wav.GetNumSamplesPerSecond(), 1, 500000);

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

    snd_pcm_drain(pcm);
    snd_pcm_close(pcm);

    return 0;
}

} // namespace nyla

auto main() -> int
{
    return nyla::Main();
}