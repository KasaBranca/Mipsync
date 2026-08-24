#include "Input.h"

namespace MipsyncEngine {

GLFWwindow* Input::s_Window = nullptr;
glm::vec2 Input::s_LastMousePos = { 0.0f, 0.0f };
glm::vec2 Input::s_MouseDelta = { 0.0f, 0.0f };
float Input::s_ScrollDelta = 0.0f;
bool Input::s_FirstMouse = true;
bool Input::s_GameInputEnabled = true;
bool Input::s_CursorLocked = false;

static float s_ScrollAccumulator = 0.0f;
static bool s_PrevKeyDown[GLFW_KEY_LAST + 1] = {};
static bool s_KeyDownEdge[GLFW_KEY_LAST + 1] = {};

static void ScrollCallback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset) {
    s_ScrollAccumulator += static_cast<float>(yoffset);
}

void Input::Init(GLFWwindow* window) {
    s_Window = window;
    s_FirstMouse = true;
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
    // GLFW key tokens start at GLFW_KEY_SPACE (32). Values below that are not
    // valid keyboard keys and glfwGetKey() logs GLFW_INVALID_ENUM every frame.
    for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key) {
        const bool down = glfwGetKey(s_Window, key) == GLFW_PRESS;
        s_KeyDownEdge[key] = down && !s_PrevKeyDown[key];
        s_PrevKeyDown[key] = down;
    }

    glm::vec2 currentPos = GetMousePosition();
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
    auto state = glfwGetKey(s_Window, key);
    return state == GLFW_PRESS || state == GLFW_REPEAT;
}

bool Input::IsKeyDown(int key) {
    if (key < 0 || key > GLFW_KEY_LAST)
        return false;
    return s_KeyDownEdge[key];
}

bool Input::IsKeyReleased(int key) {
    return glfwGetKey(s_Window, key) == GLFW_RELEASE;
}

bool Input::IsMouseButtonPressed(int button) {
    return glfwGetMouseButton(s_Window, button) == GLFW_PRESS;
}

bool Input::IsMouseButtonReleased(int button) {
    return glfwGetMouseButton(s_Window, button) == GLFW_RELEASE;
}

glm::vec2 Input::GetMousePosition() {
    double x, y;
    glfwGetCursorPos(s_Window, &x, &y);
    return { static_cast<float>(x), static_cast<float>(y) };
}

float Input::GetMouseX() { return GetMousePosition().x; }
float Input::GetMouseY() { return GetMousePosition().y; }

glm::vec2 Input::GetMouseDelta() { return s_MouseDelta; }
float Input::GetScrollDelta() { return s_ScrollDelta; }

} // namespace MipsyncEngine
