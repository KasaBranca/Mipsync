#pragma once

struct GLFWwindow;

namespace MipsyncEngine {

/// ImGui backend setup (OpenGL3 or D3D12 + GLFW).
struct GfxImGui {
    static void Init(GLFWwindow* window);
    static void Shutdown();

    static void NewFrame();
    static void RenderDrawData();
};

} // namespace MipsyncEngine
