#include "nyla/audio/wave.h"
#include "nyla/commons/containers/inline_vec.h"
#include "nyla/commons/math/vec.h"
#include "nyla/commons/os/readfile.h"
#include "nyla/platform/platform_audio.h"

#include <algorithm>
#include <alsa/asoundlib.h>
#include <array>
#include <cstdint>
#include <mutex>
#include <span>
#include <thread>
#include <utility>

namespace nyla
{

struct AudioMixerCmd
{
    std::span<const float2> frames;
};

class AudioMixer
{
  public:
    void RunThread();
    void Submit(const AudioMixerCmd &cmd);

  private:
    std::mutex m_CmdMutex{};
    InlineVec<AudioMixerCmd, 4> m_CmdWrite{};
    InlineVec<AudioMixerCmd, 4> m_CmdRead{};

    struct Voice
    {
        std::span<const float2> frames;
        uint32_t consumed;
    };
    std::array<Voice, 8> m_Voices{};
};

void AudioMixer::Submit(const AudioMixerCmd &cmd)
{
    std::lock_guard<std::mutex> lock(m_CmdMutex);
    m_CmdWrite.emplace_back(cmd);
}

void AudioMixer::RunThread()
{
    g_PlatformAudio->Init({
        .sampleRate = 48000,
        .channels = 2,
        .latencyUs = 50000,
    });

    std::unique_lock<std::mutex> lock(m_CmdMutex, std::defer_lock);

    for (;;)
    {
        lock.lock();
        std::swap(m_CmdWrite, m_CmdRead);
        m_CmdWrite.clear();
        lock.unlock();

        auto *slot = m_Voices.data();
        for (const auto &cmd : m_CmdRead)
        {
            NYLA_ASSERT(slot != m_Voices.end());
            while (!slot->frames.empty())
            {
                ++slot;
                NYLA_ASSERT(slot != m_Voices.end());
            }

            slot->frames = cmd.frames;
            slot->consumed = 0;
            ++slot;
        }

        constexpr uint64_t kFrames = 48 * 20ul;
        alignas(float) std::array<std::byte, kFrames * sizeof(float2)> buf{};
        auto bufF32 = std::span{(float2 *)buf.data(), kFrames};

        for (auto &voice : m_Voices)
        {
            if (voice.frames.empty())
                continue;

            const uint32_t start = voice.consumed;
            const uint32_t end = std::min(voice.frames.size(), voice.consumed + buf.size());

            for (uint32_t i = start; i < end; ++i)
                bufF32[i] += voice.frames[i];

            if (end == voice.frames.size())
                voice = {};
            else
                voice.consumed += buf.size();
        }

        auto bufS16 = std::span{(short2 *)buf.data(), kFrames};
        for (uint32_t i = 0; i < bufF32.size(); ++i)
        {
            auto scaleToShort = [](float f) -> int16_t {
                f = std::clamp(f, -1.0f, 1.0f);
                float scale = (f < 0.0f) ? 32768.0f : 32767.0f;
                auto i = (int32_t)std::lrintf(f * scale);
                i = std::clamp(i, (int32_t)INT16_MIN, (int32_t)INT16_MAX);
                return (int16_t)i;
            };
            bufS16[i][0] = scaleToShort(bufF32[i][0]);
            bufS16[i][1] = scaleToShort(bufF32[i][1]);
        }

        g_PlatformAudio->Write(std::as_bytes(bufS16));
    }
}

auto Main() -> int
{
    std::vector<std::byte> bytes =
        ReadFile("assets/BreakoutRechargedPaddleAndBall/Ball bounce off the player paddle.wav");

    ParseWavFileResult wav = ParseWavFile(bytes);

    AudioMixer mixer{};
    std::thread thread([&mixer] -> void { mixer.RunThread(); });

    return 0;
}

} // namespace nyla

auto main() -> int
{
    return nyla::Main();
}