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
    res = m_Device->CreateCommandQueue(&directQueueDesc, IID_PPV_ARGS(&m_DirectCommandQueue));
    NYLA_ASSERT(SUCCEEDED(res));

    CreateSwapchain();

    res = m_Factory->MakeWindowAssociation(m_Window, DXGI_MWA_NO_ALT_ENTER);
    NYLA_ASSERT(SUCCEEDED(res));

    m_FrameIndex = m_Swapchain->GetCurrentBackBufferIndex();

    const D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{
        .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
        .NumDescriptors = m_NumFramesInFlight,
        .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
    };
    res = m_Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_RtvHeap));
    NYLA_ASSERT(SUCCEEDED(res));

    const D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{
        .Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
        .NumDescriptors = 1,
        .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
    };
    res = m_Device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_DsvHeap));
    NYLA_ASSERT(SUCCEEDED(res));

    const D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc{
        .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        .NumDescriptors = 256,
        .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
    };
    res = m_Device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_CbvHeap));
    NYLA_ASSERT(SUCCEEDED(res));

    const D3D12_QUERY_HEAP_DESC queryHeapDesc{
        .Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION,
        .Count = 1,
    };
    res = m_Device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&m_QueryHeap));
    NYLA_ASSERT(SUCCEEDED(res));

    m_RTVDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_CBVDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Create frame resources.
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();

        // Create a RTV and a command allocator for each frame.
        for (uint32_t n = 0; n < m_NumFramesInFlight; n++)
        {
            res = m_Swapchain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n]));
            NYLA_ASSERT(SUCCEEDED(res));

            m_Device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);

            rtvHandle.ptr += m_rtvDescriptorSize;

            ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                           IID_PPV_ARGS(&m_commandAllocators[n])));
        }
    }
}

} // namespace nyla