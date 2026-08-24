#pragma once

#include "GfxConfig.h"
#if MIPSYNC_GFX_USE_D3D12

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>

struct GLFWwindow;

namespace MipsyncEngine {

/// Minimal D3D12 device + DXGI swap chain (2 back buffers, one direct queue).
class D3D12Context {
public:
    static constexpr int kFrameCount = 2;
    static constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    D3D12Context() = default;
    ~D3D12Context();

    D3D12Context(const D3D12Context&) = delete;
    D3D12Context& operator=(const D3D12Context&) = delete;

    bool Init(GLFWwindow* window, int width, int height, bool vsync);
    void Shutdown();

    void Resize(int width, int height);

    /// Wait for GPU, reset allocator, transition back buffer to RTV, clear, open command list.
    void BeginFrame(float clearR, float clearG, float clearB, float clearA);
    /// Close list, execute, present, advance frame index.
    void EndFrame();

    ID3D12Device* GetDevice() const { return m_Device.Get(); }
    ID3D12CommandQueue* GetCommandQueue() const { return m_CommandQueue.Get(); }
    ID3D12GraphicsCommandList* GetCommandList() const { return m_CommandList.Get(); }
    ID3D12DescriptorHeap* GetSrvHeap() const { return m_SrvHeap.Get(); }
    DXGI_FORMAT GetRTVFormat() const { return kBackBufferFormat; }
    int GetFrameIndex() const { return m_FrameIndex; }
    int GetWidth() const { return m_Width; }
    int GetHeight() const { return m_Height; }
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRtvHandle() const;

private:
    bool CreateDevice();
    bool CreateSwapChain(HWND hwnd, int width, int height);
    void CreateRenderTargets();
    void CreateFrameSync();
    void WaitForGpu();
    void MoveToNextFrame();

    Microsoft::WRL::ComPtr<ID3D12Device> m_Device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_CommandQueue;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_SwapChain;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_RtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_SrvHeap;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_CommandList;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_RenderTargets[kFrameCount];
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_CommandAllocators[kFrameCount];
    Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
    UINT64 m_FenceValues[kFrameCount]{};
    HANDLE m_FenceEvent = nullptr;

    uint32_t m_RtvDescriptorSize = 0;
    uint32_t m_SrvDescriptorSize = 0;
    int m_FrameIndex = 0;
    int m_Width = 0;
    int m_Height = 0;
    bool m_Vsync = true;
    bool m_Initialized = false;
};

D3D12Context& GetD3D12Context();

} // namespace MipsyncEngine

#endif // MIPSYNC_GFX_USE_D3D12
