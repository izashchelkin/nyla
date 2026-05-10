#include "nyla/commons/audio_backend.h"

#include "nyla/commons/fmt.h"
#include "nyla/commons/headers_windows.h"
#include "nyla/commons/intrin.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/thread.h"

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
    audio_backend_callback callback;
    void *userdata;
    uint32_t sampleRate;
    uint32_t channels;
    UINT32 bufferFrames;
    thread *thread;
    uint32_t running;
};
platform_audio *manager;

void FeederMain(void *)
{
    while (AtomicLoad32(&manager->running))
    {
        DWORD wait = WaitForSingleObject(manager->event, 200);
        if (!AtomicLoad32(&manager->running))
            break;
        if (wait != WAIT_OBJECT_0)
            continue;

        UINT32 padding = 0;
        if (FAILED(manager->client->GetCurrentPadding(&padding)))
            continue;

        UINT32 avail = manager->bufferFrames - padding;
        if (avail == 0)
            continue;

        BYTE *buf = nullptr;
        if (FAILED(manager->render->GetBuffer(avail, &buf)))
            continue;

        manager->callback(manager->userdata, (int16_t *)buf, avail);

        manager->render->ReleaseBuffer(avail, 0);
    }
}

} // namespace

namespace PlatformAudio
{

void API Init(const audio_backend_settings &settings)
{
    ASSERT(settings.callback);

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ASSERT(SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE);

    manager = &RegionAlloc::Alloc<platform_audio>(RegionAlloc::g_BootstrapAlloc);
    manager->callback = settings.callback;
    manager->userdata = settings.userdata;
    manager->sampleRate = settings.sampleRate;
    manager->channels = settings.channels;

    hr = CoCreateInstance(kClsidMMDeviceEnumerator, nullptr, CLSCTX_ALL, kIidIMMDeviceEnumerator,
                          (void **)&manager->enumerator);
    ASSERT(SUCCEEDED(hr));

    hr = manager->enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &manager->device);
    ASSERT(SUCCEEDED(hr));

    hr = manager->device->Activate(kIidIAudioClient, CLSCTX_ALL, nullptr, (void **)&manager->client);
    ASSERT(SUCCEEDED(hr));

    WAVEFORMATEX wfx{
        .wFormatTag = WAVE_FORMAT_PCM,
        .nChannels = (WORD)settings.channels,
        .nSamplesPerSec = settings.sampleRate,
        .nAvgBytesPerSec = settings.sampleRate * settings.channels * 2,
        .nBlockAlign = (WORD)(settings.channels * 2),
        .wBitsPerSample = 16,
        .cbSize = 0,
    };

    REFERENCE_TIME duration = (REFERENCE_TIME)settings.latencyUs * 10;
    DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                  AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

    hr = manager->client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, duration, 0, &wfx, nullptr);
    ASSERT(SUCCEEDED(hr), "IAudioClient::Initialize: 0x%08lX", (unsigned long)hr);

    hr = manager->client->GetBufferSize(&manager->bufferFrames);
    ASSERT(SUCCEEDED(hr));

    manager->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ASSERT(manager->event);

    hr = manager->client->SetEventHandle(manager->event);
    ASSERT(SUCCEEDED(hr));

    hr = manager->client->GetService(kIidIAudioRenderClient, (void **)&manager->render);
    ASSERT(SUCCEEDED(hr));

    AtomicStore32(&manager->running, 1);

    hr = manager->client->Start();
    ASSERT(SUCCEEDED(hr));

    manager->thread = Thread::Create(RegionAlloc::g_BootstrapAlloc, &FeederMain, nullptr);
    Thread::SetName(*manager->thread, "nyla_audio");
}

void API Destroy()
{
    if (!manager)
        return;

    AtomicStore32(&manager->running, 0);
    if (manager->event)
        SetEvent(manager->event);

    if (manager->thread)
        Thread::Join(*manager->thread);

    if (manager->client)
        manager->client->Stop();

    if (manager->render)
        manager->render->Release();
    if (manager->client)
        manager->client->Release();
    if (manager->device)
        manager->device->Release();
    if (manager->enumerator)
        manager->enumerator->Release();
    if (manager->event)
        CloseHandle(manager->event);
}

auto API GetSampleRate() -> uint32_t
{
    return manager->sampleRate;
}

auto API GetChannels() -> uint32_t
{
    return manager->channels;
}

} // namespace PlatformAudio

} // namespace nyla