#include "nyla/platform/windows/platform_windows.h"

#include "nyla/commons/assert.h"
#include "nyla/commons/containers/inline_ring.h"
#include "nyla/platform/platform.h"

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

auto Platform::Impl::CreateWin() -> HWND
{
    const wchar_t CLASS_NAME[] = L"nyla";

    WNDCLASSW wc = {
        .lpfnWndProc = MainWndProc,
        .hInstance = m_HInstance,
        .lpszClassName = CLASS_NAME,
    };

    RegisterClassW(&wc);

    HWND window = CreateWindowExW(0, CLASS_NAME, L"nyla", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, m_HInstance, nullptr);
    NYLA_ASSERT(window);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    return window;
}

auto Platform::Impl::GetWindowSize(HWND window) -> PlatformWindowSize
{
    RECT rect{};
    NYLA_ASSERT(GetWindowRect(window, &rect));

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

void Platform::Init(const PlatformInitDesc &desc)
{
    NYLA_ASSERT(m_Impl);
    m_Impl->Init(desc);
}

auto Platform::CreateWin() -> PlatformWindow
{
    return {.handle = reinterpret_cast<std::uintptr_t>(m_Impl->CreateWin())};
}

auto Platform::GetWindowSize(PlatformWindow window) -> PlatformWindowSize
{
    return m_Impl->GetWindowSize(reinterpret_cast<HWND>(window.handle));
}

auto Platform::PollEvent(PlatformEvent &outEvent) -> bool
{
    return m_Impl->PollEvent(outEvent);
}

Platform *g_Platform = new Platform();

} // namespace nyla

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow)
{
    using namespace nyla;

    g_Platform->SetImpl(new Platform::Impl{});
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
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);

        impl->EnqueueEvent({
            .type = PlatformEventType::ShouldRedraw,
        });
        return 0;

    case WM_DESTROY: {
        PostQuitMessage(0);

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