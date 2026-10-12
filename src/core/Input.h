#pragma once
// ─────────────────────────────────────────────────
// Mipsync Engine — Input System
// ─────────────────────────────────────────────────

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <atomic>
#include "PS1PadInput.h"

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
    static void SetPS1PadMapping(const PS1PadMapping& mapping);
    static void SetInjectedPS1Input(uint16_t held, const std::array<float,4>& axes);
    static void ClearInjectedPS1Input();
    static void BeginInjectedPS1Frame();
    // Platform sampling boundary, shared by gameplay and deterministic runtime checks.
    static void SamplePS1Pad(const PS1PadSample& sample);
    static bool IsPS1Button(const std::string& name);
    static bool GetPS1Button(const std::string& name);
    static bool GetPS1ButtonDown(const std::string& name);
    static bool GetPS1ButtonUp(const std::string& name);
    static float GetPS1Axis(const std::string& name);

    /// When false, GetMouseDelta() returns zero (editor UI consumes movement).
    static void SetGameInputEnabled(bool enabled);
    static bool IsGameInputEnabled();

    /// GLFW_CURSOR_DISABLED while locked (play-mode FPS look).
    static void SetCursorLocked(bool locked);
    static bool IsCursorLocked();
    /// The native window hosting Game View (may be a detached editor viewport).
    static void SetGameInputWindow(GLFWwindow* window);
    static GLFWwindow* GetGameInputWindow();
    static void ForgetGameInputWindow(GLFWwindow* window);
    static void SetCursorCaptureAllowed(bool allowed);
    /// Freeze callback-fed gameplay state while an exclusive preparation worker runs.
    static void SetPreparationInProgress(bool preparing);

    static void Update();
    static void BeginGameFrame(bool accumulated);
    static void EndGameFrame();
    static void BeginGameUIFrame(bool accumulated);
    static void EndGameUIFrame();

private:
    static GLFWwindow* s_Window;
    static glm::vec2 s_CurrentMousePos;
    static glm::vec2 s_LastMousePos;
    static glm::vec2 s_MouseDelta;
    static float s_ScrollDelta;
    static bool s_FirstMouse;
    static bool s_GameInputEnabled;
    static std::atomic<bool> s_CursorLocked;
};

} // namespace MipsyncEngine
