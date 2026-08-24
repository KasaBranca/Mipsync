#include "Gfx.h"
#if MIPSYNC_GFX_USE_D3D12
#include "D3D12Context.h"
#endif

namespace MipsyncEngine {

void Gfx::Init(GLFWwindow* window, int width, int height, bool vsync) {
#if MIPSYNC_GFX_USE_D3D12
    GetD3D12Context().Init(window, width, height, vsync);
#else
    (void)window;
    (void)width;
    (void)height;
    (void)vsync;
#endif
}

void Gfx::Shutdown() {
#if MIPSYNC_GFX_USE_D3D12
    GetD3D12Context().Shutdown();
#endif
}

void Gfx::Resize(int width, int height) {
#if MIPSYNC_GFX_USE_D3D12
    GetD3D12Context().Resize(width, height);
#endif
}

void Gfx::BeginMainPass(float clearR, float clearG, float clearB, float clearA) {
#if MIPSYNC_GFX_USE_D3D12
    GetD3D12Context().BeginFrame(clearR, clearG, clearB, clearA);
#else
    (void)clearR;
    (void)clearG;
    (void)clearB;
    (void)clearA;
#endif
}

void Gfx::EndMainPass() {
#if MIPSYNC_GFX_USE_D3D12
    GetD3D12Context().EndFrame();
#endif
}

} // namespace MipsyncEngine
