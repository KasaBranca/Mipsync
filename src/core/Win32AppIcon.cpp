#include "Win32AppIcon.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shobjidl.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <imgui.h>
#include <unordered_set>
#endif

namespace MipsyncEngine::Win32AppIcon {

#ifdef _WIN32

namespace {

constexpr int kIconResourceId = 1;

HICON g_BigIcon = nullptr;
HICON g_SmallIcon = nullptr;
std::unordered_set<HWND> g_IconAppliedHwnds;

HICON LoadEmbeddedIcon(int cx, int cy) {
    return static_cast<HICON>(LoadImageW(
        GetModuleHandleW(nullptr), MAKEINTRESOURCEW(kIconResourceId), IMAGE_ICON,
        cx, cy, LR_DEFAULTCOLOR));
}

void EnsureIconsLoaded() {
    if (!g_BigIcon)
        g_BigIcon = LoadEmbeddedIcon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    if (!g_SmallIcon)
        g_SmallIcon = LoadEmbeddedIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
}

void ApplyToHwnd(HWND hwnd) {
    if (!hwnd)
        return;
    if (!g_IconAppliedHwnds.insert(hwnd).second)
        return;

    EnsureIconsLoaded();
    if (g_BigIcon) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(g_BigIcon));
        SetClassLongPtrW(hwnd, GCLP_HICON, reinterpret_cast<LONG_PTR>(g_BigIcon));
    }
    if (g_SmallIcon) {
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g_SmallIcon));
        SetClassLongPtrW(hwnd, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(g_SmallIcon));
    }
}

} // namespace

void SetAppUserModelId(const wchar_t* id) {
    if (!id || !id[0])
        return;
    // New ID avoids reusing a stale pinned-taskbar icon from an older build.
    SetCurrentProcessExplicitAppUserModelID(id);
}

void ApplyToGlfwWindow(GLFWwindow* window) {
    if (!window)
        return;
    ApplyToHwnd(glfwGetWin32Window(window));
}

void ApplyToImGuiViewports() {
    if (!ImGui::GetCurrentContext())
        return;

    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    for (ImGuiViewport* viewport : platformIo.Viewports) {
        if (!viewport || !viewport->PlatformHandle)
            continue;
        ApplyToHwnd(static_cast<HWND>(viewport->PlatformHandle));
    }
}

#else

void SetAppUserModelId(const wchar_t*) {}
void ApplyToGlfwWindow(GLFWwindow*) {}
void ApplyToImGuiViewports() {}

#endif

} // namespace MipsyncEngine::Win32AppIcon
