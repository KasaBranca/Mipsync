#include "Window.h"
#include "Win32AppIcon.h"
#include "Log.h"
#include <stb_image.h>
#include <filesystem>

namespace MipsyncEngine {

namespace {

static void TrySetWindowIconFromPng(GLFWwindow* window, const std::filesystem::path& pngPath) {
    if (!window || !std::filesystem::exists(pngPath))
        return;

    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load(pngPath.string().c_str(), &w, &h, &channels, 4);
    if (!pixels || w <= 0 || h <= 0)
        return;

    GLFWimage image{};
    image.width = w;
    image.height = h;
    image.pixels = pixels;
    glfwSetWindowIcon(window, 1, &image);
    stbi_image_free(pixels);
}

static void ApplyWindowIcon(GLFWwindow* window) {
#ifdef _WIN32
    Win32AppIcon::ApplyToGlfwWindow(window);
#else
    namespace fs = std::filesystem;
    const fs::path rel = fs::path("resources") / "icons" / "app_icon.png";
    TrySetWindowIconFromPng(window, rel);
#endif
}

} // namespace

static bool s_GLFWInitialized = false;

static void GLFWErrorCallback(int error, const char* description) {
    MIPSYNC_ERROR("GLFW Error ({0}): {1}", error, description);
}

Window::Window(const WindowProps& props) {
    Init(props);
}

Window::~Window() {
    Shutdown();
}

void Window::Init(const WindowProps& props) {
    m_Data.title  = props.title;
    m_Data.width  = props.width;
    m_Data.height = props.height;
    m_Data.vsync  = props.vsync;

#ifdef _WIN32
    if (props.appUserModelId)
        Win32AppIcon::SetAppUserModelId(props.appUserModelId);
#endif

    if (!s_GLFWInitialized) {
        int success = glfwInit();
        if (!success) {
            MIPSYNC_FATAL("Failed to initialize GLFW!");
            return;
        }
        glfwSetErrorCallback(GLFWErrorCallback);
        s_GLFWInitialized = true;
    }

    // OpenGL 3.3 Core Profile
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    // DPI awareness
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    // Dark-themed window decorations (modern GLFW)
    #ifdef GLFW_TITLEBAR_COLOR
    // Future GLFW feature
    #endif

    if (props.maximized) {
        glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    }

    MIPSYNC_INFO("Creating window: {0} ({1}x{2})", m_Data.title, m_Data.width, m_Data.height);

    m_Window = glfwCreateWindow(m_Data.width, m_Data.height, m_Data.title.c_str(), nullptr, nullptr);
    if (!m_Window) {
        MIPSYNC_FATAL("Failed to create GLFW window!");
        return;
    }

    glfwMakeContextCurrent(m_Window);
    glfwSetWindowUserPointer(m_Window, &m_Data);
    ApplyWindowIcon(m_Window);

    // Load OpenGL via GLAD
    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
        MIPSYNC_FATAL("Failed to initialize GLAD / OpenGL!");
        return;
    }

    MIPSYNC_INFO("OpenGL Info:");
    MIPSYNC_INFO("  Vendor:   {0}", (const char*)glGetString(GL_VENDOR));
    MIPSYNC_INFO("  Renderer: {0}", (const char*)glGetString(GL_RENDERER));
    MIPSYNC_INFO("  Version:  {0}", (const char*)glGetString(GL_VERSION));

    SetVSync(props.vsync);

    // Window resize callback
    glfwSetFramebufferSizeCallback(m_Window, [](GLFWwindow* window, int width, int height) {
        WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
        data.width = width;
        data.height = height;
        glViewport(0, 0, width, height);
        if (data.resizeCallback) {
            data.resizeCallback(width, height);
        }
    });

    // Get actual framebuffer size (for HiDPI)
    glfwGetFramebufferSize(m_Window, &m_Data.width, &m_Data.height);
}

void Window::OnUpdate() {
    glfwSwapBuffers(m_Window);
    glfwPollEvents();
}

bool Window::ShouldClose() const {
    return glfwWindowShouldClose(m_Window);
}

void Window::SetVSync(bool enabled) {
    glfwSwapInterval(enabled ? 1 : 0);
    m_Data.vsync = enabled;
}

void Window::Shutdown() {
    if (m_Window) {
        glfwDestroyWindow(m_Window);
        m_Window = nullptr;
    }
}

} // namespace MipsyncEngine
