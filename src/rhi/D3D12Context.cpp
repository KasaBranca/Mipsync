#include "D3D12Context.h"
#if MIPSYNC_GFX_USE_D3D12

#include "../core/Log.h"
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <array>

namespace MipsyncEngine {

namespace {

void ThrowIfFailed(HRESULT hr, const char* msg) {
    if (FAILED(hr))
        MIPSYNC_FATAL("{} (HRESULT=0x{:08X})", msg, static_cast<unsigned>(hr));
}

} // namespace

D3D12Context& GetD3D12Context() {
    static D3D12Context ctx;
    return ctx;
}

D3D12Context::~D3D12Context() {
    Shutdown();
}

bool D3D12Context::Init(GLFWwindow* window, int width, int height, bool vsync) {
    if (m_Initialized)
        return true;

    m_Vsync = vsync;
    m_Width = width;
    m_Height = height;

    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) {
        MIPSYNC_FATAL("D3D12: GLFW Win32 HWND unavailable");
        return false;
    }

    if (!CreateDevice())
        return false;
    if (!CreateSwapChain(hwnd, width, height))
        return false;

    CreateRenderTargets();
    CreateFrameSync();

    m_Initialized = true;
    MIPSYNC_INFO("D3D12 initialized ({}x{}, vsync={})", width, height, vsync);
    return true;
}

void D3D12Context::Shutdown() {
    if (!m_Initialized)
        return;

    WaitForGpu();

    if (m_FenceEvent) {
        CloseHandle(m_FenceEvent);
        m_FenceEvent = nullptr;
    }

    m_CommandList.Reset();
    for (int i = 0; i < kFrameCount; ++i) {
        m_CommandAllocators[i].Reset();
        m_RenderTargets[i].Reset();
    }
    m_SrvHeap.Reset();
    m_RtvHeap.Reset();
    m_SwapChain.Reset();
    m_CommandQueue.Reset();
    m_Device.Reset();
    m_Fence.Reset();

    m_Initialized = false;
}

bool D3D12Context::CreateDevice() {
    UINT dxgiFlags = 0;
#if defined(_DEBUG)
    {
        Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
            debugController->EnableDebugLayer();
        else
            dxgiFlags = 0;
    }
#endif

    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFlags, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_Device))))
            break;
        adapter.Reset();
    }

    if (!m_Device) {
        Microsoft::WRL::ComPtr<IDXGIAdapter> warp;
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "EnumWarpAdapter");
        ThrowIfFailed(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_Device)),
                      "D3D12CreateDevice WARP");
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(m_Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_CommandQueue)),
                  "CreateCommandQueue");

    m_RtvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_SrvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
    srvHeapDesc.NumDescriptors = 64;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_Device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_SrvHeap)),
                  "CreateDescriptorHeap SRV");

    return true;
}

bool D3D12Context::CreateSwapChain(HWND hwnd, int width, int height) {
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2 swap");

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.NumDescriptors = kFrameCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(m_Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_RtvHeap)),
                  "CreateDescriptorHeap RTV");

    DXGI_SWAP_CHAIN_DESC1 swapDesc{};
    swapDesc.Width = static_cast<UINT>(width);
    swapDesc.Height = static_cast<UINT>(height);
    swapDesc.Format = kBackBufferFormat;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = kFrameCount;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(m_CommandQueue.Get(), hwnd, &swapDesc, nullptr,
                                                  nullptr, &swapChain1),
                  "CreateSwapChainForHwnd");
    ThrowIfFailed(swapChain1.As(&m_SwapChain), "SwapChain1 -> SwapChain3");
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    return true;
}

void D3D12Context::CreateRenderTargets() {
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < kFrameCount; ++i) {
        ThrowIfFailed(m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_RenderTargets[i])), "GetBuffer");
        m_Device->CreateRenderTargetView(m_RenderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += m_RtvDescriptorSize;
    }

    for (int i = 0; i < kFrameCount; ++i) {
        ThrowIfFailed(m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                       IID_PPV_ARGS(&m_CommandAllocators[i])),
                      "CreateCommandAllocator");
    }

    ThrowIfFailed(
        m_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_CommandAllocators[0].Get(),
                                    nullptr, IID_PPV_ARGS(&m_CommandList)),
        "CreateCommandList");
    m_CommandList->Close();
}

void D3D12Context::CreateFrameSync() {
    ThrowIfFailed(m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence)),
                  "CreateFence");
    m_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_FenceEvent)
        MIPSYNC_FATAL("CreateEvent failed for D3D12 fence");
    m_FenceValues[m_FrameIndex] = 0;
}

void D3D12Context::WaitForGpu() {
    const UINT64 signalValue = m_FenceValues[m_FrameIndex] + 1;
    ThrowIfFailed(m_CommandQueue->Signal(m_Fence.Get(), signalValue), "Signal fence");
    ThrowIfFailed(m_Fence->SetEventOnCompletion(signalValue, m_FenceEvent), "SetEventOnCompletion");
    WaitForSingleObject(m_FenceEvent, INFINITE);
    m_FenceValues[m_FrameIndex] = signalValue;
}

void D3D12Context::MoveToNextFrame() {
    const UINT64 currentFenceValue = m_FenceValues[m_FrameIndex];
    ThrowIfFailed(m_CommandQueue->Signal(m_Fence.Get(), currentFenceValue), "Signal frame fence");

    m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();

    if (m_Fence->GetCompletedValue() < m_FenceValues[m_FrameIndex])
        ThrowIfFailed(m_Fence->SetEventOnCompletion(m_FenceValues[m_FrameIndex], m_FenceEvent),
                      "SetEventOnCompletion frame");
}

void D3D12Context::Resize(int width, int height) {
    if (!m_Initialized || (width <= 0 || height <= 0))
        return;
    if (width == m_Width && height == m_Height)
        return;

    WaitForGpu();

    for (int i = 0; i < kFrameCount; ++i)
        m_RenderTargets[i].Reset();

    DXGI_SWAP_CHAIN_DESC desc{};
    m_SwapChain->GetDesc(&desc);
    ThrowIfFailed(m_SwapChain->ResizeBuffers(kFrameCount, static_cast<UINT>(width),
                                           static_cast<UINT>(height), desc.BufferDesc.Format,
                                           desc.Flags),
                  "ResizeBuffers");

    m_Width = width;
    m_Height = height;
    CreateRenderTargets();
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::GetCurrentRtvHandle() const {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(m_FrameIndex) * m_RtvDescriptorSize;
    return handle;
}

void D3D12Context::BeginFrame(float clearR, float clearG, float clearB, float clearA) {
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator = m_CommandAllocators[m_FrameIndex];
    ThrowIfFailed(allocator->Reset(), "CommandAllocator Reset");
    ThrowIfFailed(m_CommandList->Reset(allocator.Get(), nullptr), "CommandList Reset");

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = m_RenderTargets[m_FrameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_CommandList->ResourceBarrier(1, &barrier);

    const float clearColor[] = { clearR, clearG, clearB, clearA };
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = GetCurrentRtvHandle();
    m_CommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    m_CommandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(m_Width);
    viewport.Height = static_cast<float>(m_Height);
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissor{ 0, 0, m_Width, m_Height };
    m_CommandList->RSSetViewports(1, &viewport);
    m_CommandList->RSSetScissorRects(1, &scissor);
}

void D3D12Context::EndFrame() {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = m_RenderTargets[m_FrameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_CommandList->ResourceBarrier(1, &barrier);

    ThrowIfFailed(m_CommandList->Close(), "CommandList Close");

    ID3D12CommandList* lists[] = { m_CommandList.Get() };
    m_CommandQueue->ExecuteCommandLists(1, lists);

    m_SwapChain->Present(m_Vsync ? 1 : 0, 0);

    m_FenceValues[m_FrameIndex]++;
    MoveToNextFrame();
}

} // namespace MipsyncEngine

#endif // MIPSYNC_GFX_USE_D3D12
