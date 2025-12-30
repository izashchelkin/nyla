#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "nyla/commons/containers/inline_ring.h"
#include "nyla/platform/platform.h"
#include "nyla/platform/windows/platform_windows.h"

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

namespace nyla
{

void Platform::Impl::Init(const PlatformInitDesc &desc)
{
}

void Platform::Impl::EnqueueEvent(const PlatformEvent &event)
{
    m_EventsRing.emplace_back(event);
}

auto Platform::Impl::PollEvent(PlatformEvent &outEvent) -> bool
{
    while (m_EventsRing.empty())
    {
        MSG msg{};
        if (!PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            break;

        if (msg.message == WM_QUIT)
        {
            outEvent = {
                .type = PlatformEventType::ShouldExit,
            };
            return true;
        }

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (m_EventsRing.empty())
        return false;
    else
    {
        outEvent = m_EventsRing.pop_front();
        return true;
    }
}

auto Platform::Impl::CreateWin() -> PlatformWindow
{
    const TCHAR CLASS_NAME[] = TEXT("nyla");

    WNDCLASS wc = {
        .lpfnWndProc = MainWndProc,
        .hInstance = m_HInstance,
        .lpszClassName = CLASS_NAME,
    };

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(0, CLASS_NAME, TEXT("nyla"), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                               CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, m_HInstance, nullptr);
    CHECK(hwnd);

    PlatformWindow ret{
        .handle = reinterpret_cast<uint64_t>(hwnd),
    };
    return ret;
}

auto Platform::Impl::GetWindowSize(PlatformWindow window) -> PlatformWindowSize
{
    auto hWnd = reinterpret_cast<HWND>(window.handle);

    RECT rect{};
    CHECK(GetWindowRect(hWnd, &rect));

    return {
        .width = static_cast<uint32_t>(rect.right - rect.left),
        .height = static_cast<uint32_t>(rect.bottom - rect.top),
    };
}

auto Platform::Impl::GetHInstance() -> HINSTANCE
{
    return m_HInstance;
}

void Platform::Impl::SetHInstance(HINSTANCE hInstance)
{
    m_HInstance = hInstance;
}

//

} // namespace nyla

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow)
{
    using namespace nyla;

    g_Platform->GetImpl()->SetHInstance(hInstance);

    return PlatformMain();
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    using namespace nyla;

    auto *impl = g_Platform->GetImpl();
    switch (uMsg)
    {
    case WM_PAINT:
        impl->EnqueueEvent({
            .type = PlatformEventType::ShouldRedraw,
        });
        return 0;

    case WM_DESTROY: {
        impl->EnqueueEvent({
            .type = PlatformEventType::ShouldExit,
        });
        return 0;
    }

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}