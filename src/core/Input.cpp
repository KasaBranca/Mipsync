#include "Input.h"
#include <algorithm>
#include <iterator>

namespace MipsyncEngine {

GLFWwindow* Input::s_Window = nullptr;
glm::vec2 Input::s_CurrentMousePos = { 0.0f, 0.0f };
glm::vec2 Input::s_LastMousePos = { 0.0f, 0.0f };
glm::vec2 Input::s_MouseDelta = { 0.0f, 0.0f };
float Input::s_ScrollDelta = 0.0f;
bool Input::s_FirstMouse = true;
bool Input::s_GameInputEnabled = true;
bool Input::s_CursorLocked = false;

static float s_ScrollAccumulator = 0.0f;
static bool s_PrevKeyDown[GLFW_KEY_LAST + 1] = {};
static bool s_KeyDownEdge[GLFW_KEY_LAST + 1] = {};
static bool s_PendingKeyDownEdge[GLFW_KEY_LAST + 1] = {};

static void KeyCallback(GLFWwindow* /*window*/, int key, int /*scancode*/, int action,
                        int /*mods*/) {
    if (key < 0 || key > GLFW_KEY_LAST)
        return;
    if (action == GLFW_PRESS) {
        if (!s_PrevKeyDown[key])
            s_PendingKeyDownEdge[key] = true;
        s_PrevKeyDown[key] = true;
    } else if (action == GLFW_RELEASE) {
        s_PrevKeyDown[key] = false;
    }
}

static void WindowFocusCallback(GLFWwindow* /*window*/, int focused) {
    if (focused)
        return;
    // Like Godot's buffered input flush on focus loss: never leave a gameplay
    // key stuck if its release happened while another window owned focus.
    std::fill(std::begin(s_PrevKeyDown), std::end(s_PrevKeyDown), false);
    std::fill(std::begin(s_PendingKeyDownEdge), std::end(s_PendingKeyDownEdge), false);
}

static void ScrollCallback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset) {
    s_ScrollAccumulator += static_cast<float>(yoffset);
}

void Input::Init(GLFWwindow* window) {
    s_Window = window;
    s_FirstMouse = true;
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    s_CurrentMousePos = { static_cast<float>(x), static_cast<float>(y) };
    s_LastMousePos = s_CurrentMousePos;
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetWindowFocusCallback(window, WindowFocusCallback);
    glfwSetScrollCallback(window, ScrollCallback);
}

void Input::SetGameInputEnabled(bool enabled) {
    s_GameInputEnabled = enabled;
}

bool Input::IsGameInputEnabled() {
    return s_GameInputEnabled;
}

void Input::SetCursorLocked(bool locked) {
    if (!s_Window)
        return;
    if (s_CursorLocked == locked)
        return;
    s_CursorLocked = locked;
    glfwSetInputMode(s_Window, GLFW_CURSOR,
                     locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (locked)
        s_FirstMouse = true;
}

bool Input::IsCursorLocked() {
    return s_CursorLocked;
}

void Input::Update() {
    // Flush the callback-fed event buffer once per engine frame. This mirrors
    // Godot's accumulated-input model and replaces hundreds of glfwGetKey()
    // calls that used to run on every editor frame.
    std::copy(std::begin(s_PendingKeyDownEdge), std::end(s_PendingKeyDownEdge),
              std::begin(s_KeyDownEdge));
    std::fill(std::begin(s_PendingKeyDownEdge), std::end(s_PendingKeyDownEdge), false);

    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(s_Window, &x, &y);
    s_CurrentMousePos = { static_cast<float>(x), static_cast<float>(y) };
    const glm::vec2 currentPos = s_CurrentMousePos;
    if (s_FirstMouse) {
        s_LastMousePos = currentPos;
        s_FirstMouse = false;
    }
    s_MouseDelta = currentPos - s_LastMousePos;
    s_LastMousePos = currentPos;

    if (!s_GameInputEnabled)
        s_MouseDelta = { 0.0f, 0.0f };

    s_ScrollDelta = s_ScrollAccumulator;
    s_ScrollAccumulator = 0.0f;
}

bool Input::IsKeyPressed(int key) {
    if (key < 0 || key > GLFW_KEY_LAST)
        return false;
    return s_PrevKeyDown[key];
}

bool Input::IsKeyDown(int key) {
    if (key < 0 || key > GLFW_KEY_LAST)
        return false;
    return s_KeyDownEdge[key];
}

bool Input::IsKeyReleased(int key) {
    if (key < 0 || key > GLFW_KEY_LAST)
        return true;
    return !s_PrevKeyDown[key];
}

bool Input::IsMouseButtonPressed(int button) {
    return glfwGetMouseButton(s_Window, button) == GLFW_PRESS;
}

bool Input::IsMouseButtonReleased(int button) {
    return glfwGetMouseButton(s_Window, button) == GLFW_RELEASE;
}

glm::vec2 Input::GetMousePosition() {
    return s_CurrentMousePos;
}

float Input::GetMouseX() { return GetMousePosition().x; }
float Input::GetMouseY() { return GetMousePosition().y; }

glm::vec2 Input::GetMouseDelta() { return s_MouseDelta; }
float Input::GetScrollDelta() { return s_ScrollDelta; }

} // namespace MipsyncEngine
