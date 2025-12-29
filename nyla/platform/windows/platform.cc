#include "nyla/platform/platform.h"

namespace nyla
{

void Platform::Init(const PlatformInitDesc &desc)
{
    CHECK(!m_Impl);
    m_Impl = new Impl();
    m_Impl->Init(desc);
}

auto Platform::CreateWin() -> PlatformWindow
{
    return m_Impl->CreateWin();
}

auto Platform::GetWindowSize(PlatformWindow window) -> PlatformWindowSize
{
    return m_Impl->GetWindowSize(window);
}

auto Platform::PollEvent(PlatformEvent &outEvent) -> bool
{
    return m_Impl->PollEvent(outEvent);
}

Platform *g_Platform = new Platform();

} // namespace nyla