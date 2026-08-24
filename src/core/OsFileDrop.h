#pragma once
// ─────────────────────────────────────────────────
// Nostalty — OS file drag-and-drop queue (GLFW)
// ─────────────────────────────────────────────────

struct GLFWwindow;
#include <string>
#include <vector>

namespace MipsyncEngine {

/// Queues file paths dropped onto the GLFW window (UTF-8).
class OsFileDrop {
public:
    static void Init(GLFWwindow* window);

    /// Paths waiting to be consumed by the editor (e.g. Project panel).
    static const std::vector<std::string>& Peek();
    /// Removes and returns all pending paths.
    static std::vector<std::string> Consume();
    static void Clear();

private:
    static void DropCallback(GLFWwindow* window, int count, const char** paths);
};

} // namespace MipsyncEngine
