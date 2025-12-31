#include "nyla/commons/assert.h"
#include "nyla/rhi/d3d12/rhi_d3d12.h"
#include "nyla/rhi/rhi.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

namespace nyla
{

template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

void Rhi::Impl::Init(const RhiInitDesc &rhiDesc)
{
    NYLA_ASSERT(rhiDesc.numFramesInFlight <= kRhiMaxNumFramesInFlight);
    if (rhiDesc.numFramesInFlight)
    {
        m_NumFramesInFlight = rhiDesc.numFramesInFlight;
    }
    else
    {
        m_NumFramesInFlight = 2;
    }

    m_Flags = rhiDesc.flags;
    m_Window = reinterpret_cast<HWND>(rhiDesc.window.handle);

    uint32_t dxgiFactoryFlags = 0;

#if !defined(NDEBUG)
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();

            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    HRESULT res;

    res = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_Factory));
    NYLA_ASSERT(SUCCEEDED(res));

    const DXGI_GPU_PREFERENCE gpuPreference = DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;

    ComPtr<IDXGIAdapter1> adapter;

    if (ComPtr<IDXGIFactory6> factory6; SUCCEEDED(m_Factory->QueryInterface(IID_PPV_ARGS(&factory6))))
    {
        for (uint32_t i = 0; SUCCEEDED(factory6->EnumAdapterByGpuPreference(i, gpuPreference, IID_PPV_ARGS(&adapter)));
             ++i)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                continue;

            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
                break;
        }
    }

    if (!adapter.Get())
    {
        for (uint32_t i = 0; SUCCEEDED(m_Factory->EnumAdapters1(i, &adapter)); ++i)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                continue;

            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
                break;
        }
    }

    NYLA_ASSERT(adapter.Get());
    res = adapter.As(&m_Adapter);
    NYLA_ASSERT(SUCCEEDED(res));

    res = D3D12CreateDevice(m_Adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_Device));
    NYLA_ASSERT(SUCCEEDED(res));

    D3D12_COMMAND_QUEUE_DESC directQueueDesc = {
        .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
    };
    NYLA_ASSERT(m_Device->CreateCommandQueue(&directQueueDesc, IID_PPV_ARGS(&m_DirectCommandQueue)));

    CreateSwapchain();

    res = m_Factory->MakeWindowAssociation(m_Window, DXGI_MWA_NO_ALT_ENTER);
    NYLA_ASSERT(SUCCEEDED(res));

    m_FrameIndex = m_Swapchain->GetCurrentBackBufferIndex();

#if 0
    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        // Create a RTV for each frame.
        for (UINT n = 0; n < FrameCount; n++)
        {
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
            m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, m_rtvDescriptorSize);
        }
    }

    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
#endif
}

} // namespace nyla