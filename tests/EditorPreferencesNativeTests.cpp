#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_WIN32
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#define main PreferencesRegressionMain
#include "EditorPreferencesTests.cpp"
#undef main
#include <fstream>

int main() try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Require(glfwInit(), "GLFW init failed");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    auto* host = glfwCreateWindow(1200, 900, "Preferences test host", nullptr, nullptr);
    Require(host != nullptr, "host creation failed");
    glfwMakeContextCurrent(host);
    Require(gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress)), "OpenGL loader failed");
    ImGui::CreateContext();
    auto& io = ImGui::GetIO(); io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable | ImGuiConfigFlags_DockingEnable;
    EditorTheme::Apply(); EditorTheme::LoadFonts(14);
    ImGui_ImplGlfw_InitForOpenGL(host, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    // Create genuine native windows with the editor backend, keeping the test hidden.
    ImGui::GetPlatformIO().Platform_ShowWindow = [](ImGuiViewport*) {};
    EditorPreferences settings; settings.Load(nullptr, 14);
    EditorPreferencesWindow window; window.Open("Scene View");
    auto frame = [&] {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
        window.Draw(settings); ImGui::Render();
        ImGui::UpdatePlatformWindows(); ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(host);
    };
    frame(); frame(); frame();
    auto* root = ImGui::FindWindowByName("Preferences");
    Require(root && root->ViewportOwned && root->Viewport->PlatformHandle, "no native Preferences window");
    auto* native = static_cast<GLFWwindow*>(root->Viewport->PlatformHandle);
    Require(native != host && glfwGetWin32Window(native) != glfwGetWin32Window(host), "Preferences shares host native window");
    Require(glfwGetWindowAttrib(native, GLFW_DECORATED), "native window decorations disabled");
    const auto style = GetWindowLongPtrW(glfwGetWin32Window(native), GWL_STYLE);
    Require((style & WS_CAPTION) && (style & WS_MAXIMIZEBOX) && (style & WS_MINIMIZEBOX), "native window controls missing");
    Require(root->TitleBarHeight == 0, "duplicate ImGui caption remains");
    auto checkFrame = [&] {
        const HWND hwnd = glfwGetWin32Window(native);
        RECT outer{}, client{}; POINT origin{};
        GetWindowRect(hwnd, &outer); GetClientRect(hwnd, &client); ClientToScreen(hwnd, &origin);
        std::fprintf(stderr, "Frame: outer=(%ld,%ld)-(%ld,%ld), client=%ldx%ld at (%ld,%ld), ImGui=(%.0f,%.0f) %.0fx%.0f\n",
            outer.left,outer.top,outer.right,outer.bottom,client.right,client.bottom,origin.x,origin.y,
            root->Pos.x,root->Pos.y,root->Size.x,root->Size.y);
        Require(origin.x - outer.left == 1 && outer.right - origin.x - client.right == 1,
                "caption overhangs client width");
        Require(outer.bottom - origin.y - client.bottom == 1, "bottom frame gutter remains");
        Require(std::abs(client.right - root->Size.x) <= 1 && std::abs(client.bottom - root->Size.y) <= 1,
                "native client and ImGui size differ");
    };
    checkFrame();
    // Use the same platform calls as a resize/move from ImGui. Default GLFW
    // assumes eight-pixel gutters; those assumptions must not grow or shift us.
    ImGui::GetPlatformIO().Platform_SetWindowSize(root->Viewport, {720, 420});
    ImGui::GetPlatformIO().Platform_SetWindowPos(root->Viewport, {140, 160});
    frame(); frame(); frame(); checkFrame();
    Require(root->Size.x == 720 && root->Size.y == 420, "resize accumulated native gutters");
    Require(root->Pos.x == 140 && root->Pos.y == 160, "move drifted by the native gutter");
    int width, height; glfwGetFramebufferSize(native, &width, &height);
    glfwMakeContextCurrent(native);
    glViewport(0, 0, width, height); glClearColor(.1f, .1f, .1f, 1); glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(root->Viewport->DrawData);
    std::vector<unsigned char> pixels(width * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream image("tmp/preferences-native.ppm", std::ios::binary);
    image << "P6\n" << width << " " << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y) for (int x = 0; x < width; ++x)
        image.write(reinterpret_cast<const char*>(&pixels[(y * width + x) * 4]), 3);
    image.close();
    glfwMakeContextCurrent(host);
    for (auto* category : EditorPreferenceCategories()) { window.Open(category); frame(); frame(); }
    Require(ImGui::GetPlatformIO().Viewports.Size == 2, "categories spawned duplicate native windows");
    PostMessageW(glfwGetWin32Window(native), WM_CLOSE, 0, 0);
    frame(); frame();
    Require(!window.IsOpen(), "native title-bar close failed");
    window.Open("Game View"); frame(); frame();
    Require(window.IsOpen() && window.Category() == "Game View", "native reopen failed");
    native = static_cast<GLFWwindow*>(root->Viewport->PlatformHandle);
    checkFrame();
    ImGui::DestroyPlatformWindows();
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
    glfwDestroyWindow(host); glfwTerminate();
    std::puts("Native Preferences: aligned frame, single caption, resize/move, separate HWND, native controls, GL rendering, all categories, close and reopen passed.");
    return 0;
} catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
