#pragma once

#include <cstdint>
#include <span>

namespace nyla
{

struct PlatformAudioInitDesc
{
    uint32_t sampleRate;
    uint32_t channels;
    uint32_t latencyUs;
};

class PlatformAudio
{
  public:
    void Init(const PlatformAudioInitDesc &);
    void Destroy();
    void Write(std::span<const std::byte> data);

  private:
    class Impl;
    Impl *m_Impl;
};

extern PlatformAudio *g_PlatformAudio;

} // namespace nyla