#pragma once

#include "GfxConfig.h"
#include <GLFW/glfw3.h>

namespace MipsyncEngine {

/// Cross-backend graphics bootstrap (OpenGL legacy vs DirectX 12).
class Gfx {
public:
    static bool UsesD3D12() { return MIPSYNC_GFX_USE_D3D12 != 0; }

    static void Init(GLFWwindow* window, int width, int height, bool vsync);
    static void Shutdown();

    static void Resize(int width, int height);

    /// Main swap chain / default framebuffer clear (editor background).
    static void BeginMainPass(float clearR, float clearG, float clearB, float clearA);
    static void EndMainPass();
};

} // namespace MipsyncEngine
