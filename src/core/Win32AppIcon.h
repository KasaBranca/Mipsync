#pragma once

struct GLFWwindow;

namespace MipsyncEngine {

/// Win32 taskbar / pin identity and embedded .ico (resource id 1) helpers.
namespace Win32AppIcon {

/// Call once per process before creating the first GLFW window.
void SetAppUserModelId(const wchar_t* id);

/// Apply exe-embedded icon to a GLFW window (WM_SETICON + class icons).
void ApplyToGlfwWindow(GLFWwindow* window);

/// ImGui multi-viewport: child OS windows default to blank icons unless updated.
void ApplyToImGuiViewports();

} // namespace Win32AppIcon

} // namespace MipsyncEngine
