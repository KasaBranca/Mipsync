#include "GfxImGui.h"
#include "GfxConfig.h"
#include <imgui.h>

#if MIPSYNC_GFX_USE_D3D12
#include "D3D12Context.h"
#include <imgui_impl_dx12.h>
#include <imgui_impl_glfw.h>
#else
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#endif

namespace MipsyncEngine {

#if MIPSYNC_GFX_USE_D3D12
static bool s_ImGuiD3D12Ready = false;
#endif

void GfxImGui::Init(GLFWwindow* window) {
#if MIPSYNC_GFX_USE_D3D12
    ImGui_ImplGlfw_InitForOtherApi(window, true);

    D3D12Context& dx = GetD3D12Context();
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = 1;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ImGui_ImplDX12_InitInfo initInfo{};
    initInfo.Device = dx.GetDevice();
    initInfo.CommandQueue = dx.GetCommandQueue();
    initInfo.NumFramesInFlight = D3D12Context::kFrameCount;
    initInfo.RTVFormat = D3D12Context::kBackBufferFormat;
    initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
    initInfo.SrvDescriptorHeap = dx.GetSrvHeap();
    initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu,
                                       D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) {
        D3D12Context& ctx = GetD3D12Context();
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = ctx.GetSrvHeap()->GetCPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE gpu = ctx.GetSrvHeap()->GetGPUDescriptorHandleForHeapStart();
        *outCpu = cpu;
        *outGpu = gpu;
    };
    initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE,
                                      D3D12_GPU_DESCRIPTOR_HANDLE) {};

    ImGui_ImplDX12_Init(&initInfo);
    s_ImGuiD3D12Ready = true;
#else
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
#endif
}

void GfxImGui::Shutdown() {
#if MIPSYNC_GFX_USE_D3D12
    if (s_ImGuiD3D12Ready) {
        ImGui_ImplDX12_Shutdown();
        s_ImGuiD3D12Ready = false;
    }
    ImGui_ImplGlfw_Shutdown();
#else
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
#endif
}

void GfxImGui::NewFrame() {
#if MIPSYNC_GFX_USE_D3D12
    ImGui_ImplDX12_NewFrame();
#else
    ImGui_ImplOpenGL3_NewFrame();
#endif
    ImGui_ImplGlfw_NewFrame();
}

void GfxImGui::RenderDrawData() {
#if MIPSYNC_GFX_USE_D3D12
    D3D12Context& dx = GetD3D12Context();
    ID3D12DescriptorHeap* heaps[] = { dx.GetSrvHeap() };
    dx.GetCommandList()->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dx.GetCommandList());
#else
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif
}

} // namespace MipsyncEngine
