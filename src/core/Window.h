#pragma once
// ─────────────────────────────────────────────────
// Mipsync Engine — Window Management (GLFW)
// ─────────────────────────────────────────────────

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <string>
#include <functional>

namespace MipsyncEngine {

struct WindowProps {
    std::string title = "Mipsync Engine";
    int width  = 1600;
    int height = 900;
    bool vsync = true;
    bool maximized = true;
    /// Win32 only: taskbar pin identity (must be set before the window is created).
    const wchar_t* appUserModelId = nullptr;
};

class Window {
public:
    Window(const WindowProps& props = WindowProps{});
    ~Window();

    void OnUpdate();
    bool ShouldClose() const;

    int GetWidth() const { return m_Data.width; }
    int GetHeight() const { return m_Data.height; }
    float GetAspectRatio() const { return static_cast<float>(m_Data.width) / static_cast<float>(m_Data.height); }

    void SetVSync(bool enabled);
    bool IsVSync() const { return m_Data.vsync; }

    GLFWwindow* GetNativeWindow() const { return m_Window; }

    // Callback for window resize
    using ResizeCallback = std::function<void(int, int)>;
    void SetResizeCallback(ResizeCallback cb) { m_Data.resizeCallback = std::move(cb); }

private:
    void Init(const WindowProps& props);
    void Shutdown();

    GLFWwindow* m_Window = nullptr;

    struct WindowData {
        std::string title;
        int width, height;
        bool vsync;
        ResizeCallback resizeCallback;
    };

    WindowData m_Data;
};

} // namespace MipsyncEngine
