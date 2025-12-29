#include "nyla/platform/platform.h"

namespace nyla
{

class Platform::Impl
{
  public:
    void Init(const PlatformInitDesc &desc);
    auto CreateWin() -> PlatformWindow;
    auto PollEvent(PlatformEvent &outEvent) -> bool;
    void EnqueueEvent(const PlatformEvent &);
    auto GetWindowSize(PlatformWindow window) -> PlatformWindowSize;
    void SetHInstance(void* hInstance);

  private:
    InlineRing<PlatformEvent, 8> m_EventsRing;
    void* m_HInstance;
};

} // namespace nyla