#include "nyla/commons/assert.h"
#include "nyla/platform/platform.h"
#include "nyla/platform/windows/platform_windows.h"
#include "nyla/rhi/d3d12/rhi_d3d12.h"

namespace nyla
{

void Rhi::Impl::CreateSwapchain()
{
    const PlatformWindowSize windowSize = g_Platform->GetImpl()->GetWindowSize(m_Window);

    const DXGI_SWAP_CHAIN_DESC1 swapchainDesc{
        .Width = windowSize.width,
        .Height = windowSize.height,
        .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
        .SampleDesc =
            {
                .Count = 1,
            },
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = m_NumFramesInFlight,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
    };

    ID3D12CommandQueue *directQueue = m_DirectCommandQueue.Get();

    ComPtr<IDXGISwapChain1> swapchain;

    HRESULT res;
    res = m_Factory->CreateSwapChainForHwnd(directQueue, m_Window, &swapchainDesc, nullptr, nullptr,
                                            swapchain.GetAddressOf());
    NYLA_ASSERT(SUCCEEDED(res));

    res = swapchain.As(&m_Swapchain);
    NYLA_ASSERT(SUCCEEDED(res));
}

} // namespace nyla