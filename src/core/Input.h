#pragma once
// ─────────────────────────────────────────────────
// Mipsync Engine — Input System
// ─────────────────────────────────────────────────

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

namespace MipsyncEngine {

class Input {
public:
    static void Init(GLFWwindow* window);

    static bool IsKeyPressed(int key);
    /// True only on the frame the key transitioned to pressed.
    static bool IsKeyDown(int key);
    static bool IsKeyReleased(int key);
    static bool IsMouseButtonPressed(int button);
    static bool IsMouseButtonReleased(int button);

    static glm::vec2 GetMousePosition();
    static float GetMouseX();
    static float GetMouseY();

    /// Mouse delta for the current frame (zero when game input is disabled).
    static glm::vec2 GetMouseDelta();
    static float GetScrollDelta();

    /// When false, GetMouseDelta() returns zero (editor UI consumes movement).
    static void SetGameInputEnabled(bool enabled);
    static bool IsGameInputEnabled();

    /// GLFW_CURSOR_DISABLED while locked (play-mode FPS look).
    static void SetCursorLocked(bool locked);
    static bool IsCursorLocked();

    static void Update();

private:
    static GLFWwindow* s_Window;
    static glm::vec2 s_CurrentMousePos;
    static glm::vec2 s_LastMousePos;
    static glm::vec2 s_MouseDelta;
    static float s_ScrollDelta;
    static bool s_FirstMouse;
    static bool s_GameInputEnabled;
    static bool s_CursorLocked;
};

} // namespace MipsyncEngine
