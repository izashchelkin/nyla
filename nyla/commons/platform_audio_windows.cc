#include "nyla/commons/platform_audio.h"

#include "nyla/commons/fmt.h"
#include "nyla/commons/headers_windows.h"
#include "nyla/commons/intrin.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/platform_thread.h"
#include "nyla/commons/region_alloc.h"

#include <cstdint>

namespace nyla
{

namespace
{

const CLSID kClsidMMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID kIidIMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID kIidIAudioClient = __uuidof(IAudioClient);
const IID kIidIAudioRenderClient = __uuidof(IAudioRenderClient);

struct platform_audio
{
    IMMDeviceEnumerator *enumerator;
    IMMDevice *device;
    IAudioClient *client;
    IAudioRenderClient *render;
    HANDLE event;
    PlatformAudioCallback callback;
    void *user;
    uint32_t sampleRate;
    uint32_t channels;
    UINT32 bufferFrames;
    platform_thread *thread;
    uint32_t running;
};
platform_audio *audio;

void FeederMain(void *)
{
    while (AtomicLoad32(&audio->running))
    {
        DWORD wait = WaitForSingleObject(audio->event, 200);
        if (!AtomicLoad32(&audio->running))
            break;
        if (wait != WAIT_OBJECT_0)
            continue;

        UINT32 padding = 0;
        if (FAILED(audio->client->GetCurrentPadding(&padding)))
            continue;

        UINT32 avail = audio->bufferFrames - padding;
        if (avail == 0)
            continue;

        BYTE *buf = nullptr;
        if (FAILED(audio->render->GetBuffer(avail, &buf)))
            continue;

        audio->callback(audio->user, (int16_t *)buf, avail);

        audio->render->ReleaseBuffer(avail, 0);
    }
}

} // namespace

namespace PlatformAudio
{

void API Init(const PlatformAudioInitDesc &desc)
{
    ASSERT(!audio);
    ASSERT(desc.callback);

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ASSERT(SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE);

    audio = &RegionAlloc::Alloc<platform_audio>(RegionAlloc::g_BootstrapAlloc);
    audio->callback = desc.callback;
    audio->user = desc.user;
    audio->sampleRate = desc.sampleRate;
    audio->channels = desc.channels;

    hr = CoCreateInstance(kClsidMMDeviceEnumerator, nullptr, CLSCTX_ALL, kIidIMMDeviceEnumerator,
                          (void **)&audio->enumerator);
    ASSERT(SUCCEEDED(hr));

    hr = audio->enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &audio->device);
    ASSERT(SUCCEEDED(hr));

    hr = audio->device->Activate(kIidIAudioClient, CLSCTX_ALL, nullptr, (void **)&audio->client);
    ASSERT(SUCCEEDED(hr));

    WAVEFORMATEX wfx{};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = (WORD)desc.channels;
    wfx.nSamplesPerSec = desc.sampleRate;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (WORD)(desc.channels * 2);
    wfx.nAvgBytesPerSec = desc.sampleRate * desc.channels * 2;
    wfx.cbSize = 0;

    REFERENCE_TIME duration = (REFERENCE_TIME)desc.latencyUs * 10;
    DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                  AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

    hr = audio->client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, duration, 0, &wfx, nullptr);
    if (FAILED(hr))
    {
        LOG("IAudioClient::Initialize: 0x%08lX", (unsigned long)hr);
        ASSERT(false);
    }

    hr = audio->client->GetBufferSize(&audio->bufferFrames);
    ASSERT(SUCCEEDED(hr));

    audio->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ASSERT(audio->event);

    hr = audio->client->SetEventHandle(audio->event);
    ASSERT(SUCCEEDED(hr));

    hr = audio->client->GetService(kIidIAudioRenderClient, (void **)&audio->render);
    ASSERT(SUCCEEDED(hr));

    AtomicStore32(&audio->running, 1);

    hr = audio->client->Start();
    ASSERT(SUCCEEDED(hr));

    audio->thread = PlatformThread::Create(RegionAlloc::g_BootstrapAlloc, &FeederMain, nullptr);
    PlatformThread::SetName(*audio->thread, "nyla-audio");
}

void API Destroy()
{
    if (!audio)
        return;

    AtomicStore32(&audio->running, 0);
    if (audio->event)
        SetEvent(audio->event);

    if (audio->thread)
        PlatformThread::Join(*audio->thread);

    if (audio->client)
        audio->client->Stop();

    if (audio->render)
        audio->render->Release();
    if (audio->client)
        audio->client->Release();
    if (audio->device)
        audio->device->Release();
    if (audio->enumerator)
        audio->enumerator->Release();
    if (audio->event)
        CloseHandle(audio->event);
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