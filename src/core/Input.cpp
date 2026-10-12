#include "Input.h"
#include "GameInputBuffer.h"
#include <algorithm>
#include <iterator>
#include <atomic>
#include <thread>

namespace MipsyncEngine {

GLFWwindow* Input::s_Window = nullptr;
glm::vec2 Input::s_CurrentMousePos = { 0.0f, 0.0f };
glm::vec2 Input::s_LastMousePos = { 0.0f, 0.0f };
glm::vec2 Input::s_MouseDelta = { 0.0f, 0.0f };
float Input::s_ScrollDelta = 0.0f;
bool Input::s_FirstMouse = true;
bool Input::s_GameInputEnabled = true;
std::atomic<bool> Input::s_CursorLocked{false};
static bool s_CursorCaptureAllowed = true;
static GLFWwindow* s_GameInputWindow = nullptr;
static std::atomic<bool> s_Preparing{false};
static std::thread::id s_PreparationUIThread;

static float s_ScrollAccumulator = 0.0f;
static bool s_PrevKeyDown[GLFW_KEY_LAST + 1] = {};
static bool s_KeyDownEdge[GLFW_KEY_LAST + 1] = {};
static bool s_PendingKeyDownEdge[GLFW_KEY_LAST + 1] = {};
static bool s_KeyUpEdge[GLFW_KEY_LAST + 1] = {};
static bool s_PendingKeyUpEdge[GLFW_KEY_LAST + 1] = {};
static bool s_MouseHeld[GLFW_MOUSE_BUTTON_LAST + 1] = {};
static bool s_PendingMouseDown[GLFW_MOUSE_BUTTON_LAST + 1] = {};
static bool s_PendingMouseUp[GLFW_MOUSE_BUTTON_LAST + 1] = {};
static PS1PadInput s_PadInput;
static uint16_t s_InjectedHeld=0, s_PreviousInjectedHeld=0;
static std::array<float,4> s_InjectedAxes{};
static PS1PadFrame s_InjectedUIFrame;
static bool s_InjectedUIPending=false;
static bool s_UsePadFrame = false;
static double s_PadLastTime = 0;
static bool s_ResetMouseDelta = false;
static GameInputBuffer<GLFW_KEY_LAST + 1> s_GameBuffer;
static GameInputBuffer<GLFW_KEY_LAST + 1> s_GameFrameBuffer;
static bool s_UseGameBuffer = false;

static void KeyCallback(GLFWwindow* /*window*/, int key, int /*scancode*/, int action,
                        int /*mods*/) {
    if (s_Preparing || key < 0 || key > GLFW_KEY_LAST)
        return;
    if (action == GLFW_PRESS) {
        if (!s_PrevKeyDown[key])
            s_PendingKeyDownEdge[key] = true;
        s_PrevKeyDown[key] = true;
    } else if (action == GLFW_RELEASE) {
        if (s_PrevKeyDown[key]) s_PendingKeyUpEdge[key] = true;
        s_PrevKeyDown[key] = false;
    }
}

static void WindowFocusCallback(GLFWwindow* /*window*/, int focused) {
    if (s_Preparing) return;
    s_ResetMouseDelta = true;
    if (focused)
        return;
    // Like Godot's buffered input flush on focus loss: never leave a gameplay
    // key stuck if its release happened while another window owned focus.
    std::fill(std::begin(s_PrevKeyDown), std::end(s_PrevKeyDown), false);
    std::fill(std::begin(s_PendingKeyDownEdge), std::end(s_PendingKeyDownEdge), false);
    s_GameBuffer.Clear();
    s_GameFrameBuffer.Clear();
    std::fill(std::begin(s_PendingKeyUpEdge), std::end(s_PendingKeyUpEdge), false);
    std::fill(std::begin(s_KeyDownEdge), std::end(s_KeyDownEdge), false);
    std::fill(std::begin(s_KeyUpEdge), std::end(s_KeyUpEdge), false);
    std::fill(std::begin(s_MouseHeld), std::end(s_MouseHeld), false);
    std::fill(std::begin(s_PendingMouseDown), std::end(s_PendingMouseDown), false);
    std::fill(std::begin(s_PendingMouseUp), std::end(s_PendingMouseUp), false);
    s_PadInput.Clear();
}

static void ScrollCallback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset) {
    if (s_Preparing) return;
    s_ScrollAccumulator += static_cast<float>(yoffset);
}

static void MouseButtonCallback(GLFWwindow*, int button, int action, int) {
    if (s_Preparing || button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return;
    if (action == GLFW_PRESS) {
        if (!s_MouseHeld[button]) s_PendingMouseDown[button] = true;
        s_MouseHeld[button] = true;
    } else if (action == GLFW_RELEASE) {
        if (s_MouseHeld[button]) s_PendingMouseUp[button] = true;
        s_MouseHeld[button] = false;
    }
}

void Input::Init(GLFWwindow* window) {
    s_Window = window;
    s_GameInputWindow = window;
    s_CursorLocked = false;
    s_FirstMouse = true;
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    s_CurrentMousePos = { static_cast<float>(x), static_cast<float>(y) };
    s_LastMousePos = s_CurrentMousePos;
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetWindowFocusCallback(window, WindowFocusCallback);
    glfwSetScrollCallback(window, ScrollCallback);
    s_PadLastTime = glfwGetTime();
}

void Input::SetGameInputEnabled(bool enabled) {
    if (s_GameInputEnabled != enabled) { s_GameBuffer.Clear(); s_PadInput.Clear(); }
    s_GameInputEnabled = enabled;
}

bool Input::IsGameInputEnabled() {
    return s_GameInputEnabled;
}

void Input::SetCursorLocked(bool locked) {
    const bool preparing = s_Preparing.load();
    // Scripts in Awake/Start run on the preparation worker. The UI thread
    // must still be able to release the native cursor on Esc or focus loss.
    if (preparing && std::this_thread::get_id() != s_PreparationUIThread) return;
    if (locked && !s_CursorCaptureAllowed) return;
    GLFWwindow* window = GetGameInputWindow();
    if (!window)
        return;
    const int mode = locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL;
    const bool changed = s_CursorLocked != locked || glfwGetInputMode(window, GLFW_CURSOR) != mode;
    const bool raw = glfwRawMouseMotionSupported() == GLFW_TRUE;
    if (!changed && (!raw || glfwGetInputMode(window, GLFW_RAW_MOUSE_MOTION) == (locked ? GLFW_TRUE : GLFW_FALSE)))
        return;
    s_CursorLocked = locked;
    // DISABLED supplies unbounded relative motion and confines the OS pointer.
    // HIDDEN alone would still let it hit screen edges or leave the Game View.
    glfwSetInputMode(window, GLFW_CURSOR, mode);
    if (raw) glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, locked ? GLFW_TRUE : GLFW_FALSE);
    if (changed && !preparing) {
        s_FirstMouse = true;
        s_MouseDelta = {0.0f, 0.0f};
        s_GameBuffer.Clear();
        s_GameFrameBuffer.Clear();
        s_PadInput.Clear();
    }
}

bool Input::IsCursorLocked() {
    if (s_Preparing) return s_CursorLocked.load(); // No GLFW calls from Awake/Start.
    GLFWwindow* window = GetGameInputWindow();
    return s_CursorLocked && window && glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
}

GLFWwindow* Input::GetGameInputWindow() {
    return s_GameInputWindow ? s_GameInputWindow : s_Window;
}

void Input::SetGameInputWindow(GLFWwindow* window) {
    if (!window) window = s_Window;
    if (GetGameInputWindow() == window) return;
    SetCursorLocked(false);
    s_GameInputWindow = window;
    s_FirstMouse = true;
    s_GameBuffer.Clear();
    s_GameFrameBuffer.Clear();
    s_PadInput.Clear();
}

void Input::ForgetGameInputWindow(GLFWwindow* window) {
    if (window && window == GetGameInputWindow()) SetGameInputWindow(nullptr);
}

void Input::Update() {
    if (s_ResetMouseDelta) { s_FirstMouse = true; s_ResetMouseDelta = false; }
    // Flush the callback-fed event buffer once per engine frame. This mirrors
    // Godot's accumulated-input model and replaces hundreds of glfwGetKey()
    // calls that used to run on every editor frame.
    std::copy(std::begin(s_PendingKeyDownEdge), std::end(s_PendingKeyDownEdge),
              std::begin(s_KeyDownEdge));
    std::fill(std::begin(s_PendingKeyDownEdge), std::end(s_PendingKeyDownEdge), false);

    std::copy(std::begin(s_PendingKeyUpEdge), std::end(s_PendingKeyUpEdge), std::begin(s_KeyUpEdge));
    std::fill(std::begin(s_PendingKeyUpEdge), std::end(s_PendingKeyUpEdge), false);
    double x = 0.0;
    double y = 0.0;
    GLFWwindow* gameWindow = GetGameInputWindow();
    glfwGetCursorPos(gameWindow, &x, &y);
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
    s_GameBuffer.Accumulate(s_KeyDownEdge, s_MouseDelta, s_ScrollDelta);
    std::array<bool,24> held{}, down{}, up{};
    for (size_t i=0; i<held.size(); ++i) {
        const int binding = s_PadInput.mapping.bindings[i];
        if (binding >= 1000 && binding <= 1000+GLFW_MOUSE_BUTTON_LAST) {
            held[i]=s_MouseHeld[binding-1000]; down[i]=s_PendingMouseDown[binding-1000]; up[i]=s_PendingMouseUp[binding-1000];
        } else if (binding >= 0 && binding <= GLFW_KEY_LAST) {
            held[i]=s_PrevKeyDown[binding]; down[i]=s_KeyDownEdge[binding]; up[i]=s_KeyUpEdge[binding];
        }
    }
    uint16_t physicalButtons=0;
    std::array<float,4> axes{};
    GLFWgamepadstate pad{};
    if (glfwGetGamepadState(GLFW_JOYSTICK_1,&pad)) {
        constexpr int buttons[16]={GLFW_GAMEPAD_BUTTON_A,GLFW_GAMEPAD_BUTTON_B,
            GLFW_GAMEPAD_BUTTON_X,GLFW_GAMEPAD_BUTTON_Y,GLFW_GAMEPAD_BUTTON_START,
            GLFW_GAMEPAD_BUTTON_BACK,GLFW_GAMEPAD_BUTTON_LEFT_BUMPER,GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER,
            -1,-1,GLFW_GAMEPAD_BUTTON_LEFT_THUMB,GLFW_GAMEPAD_BUTTON_RIGHT_THUMB,
            GLFW_GAMEPAD_BUTTON_DPAD_UP,GLFW_GAMEPAD_BUTTON_DPAD_DOWN,
            GLFW_GAMEPAD_BUTTON_DPAD_LEFT,GLFW_GAMEPAD_BUTTON_DPAD_RIGHT};
        for (int i=0;i<16;++i) if (buttons[i]>=0 && pad.buttons[buttons[i]]==GLFW_PRESS) physicalButtons |= 1u<<i;
        if (pad.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER]>0.f) physicalButtons |= 1u<<8;
        if (pad.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER]>0.f) physicalButtons |= 1u<<9;
        axes={pad.axes[GLFW_GAMEPAD_AXIS_LEFT_X],-pad.axes[GLFW_GAMEPAD_AXIS_LEFT_Y],
            pad.axes[GLFW_GAMEPAD_AXIS_RIGHT_X],-pad.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]};
    }
    const double now=glfwGetTime();
    SamplePS1Pad({held,down,up,physicalButtons,axes,{s_MouseDelta.x,s_MouseDelta.y},
        static_cast<float>(std::max(0.,now-s_PadLastTime)),
        glfwGetWindowAttrib(gameWindow,GLFW_FOCUSED)!=0});
    s_PadLastTime=now;
    std::fill(std::begin(s_PendingMouseDown),std::end(s_PendingMouseDown),false);
    std::fill(std::begin(s_PendingMouseUp),std::end(s_PendingMouseUp),false);
}

void Input::BeginGameFrame(bool accumulated) {
    s_GameFrameBuffer = s_GameBuffer;
    s_UseGameBuffer = accumulated;
    s_PadInput.BeginFrame(); s_UsePadFrame = true;
}
void Input::EndGameFrame() { s_UseGameBuffer = s_UsePadFrame = false; s_GameBuffer.Clear(); s_PadInput.Consume(); }
void Input::BeginGameUIFrame(bool accumulated) { s_UseGameBuffer = accumulated; s_UsePadFrame = true;if(s_InjectedUIPending)s_PadInput.frame=s_InjectedUIFrame; }
void Input::EndGameUIFrame() { s_UseGameBuffer = s_UsePadFrame = false;s_InjectedUIPending=false; s_GameFrameBuffer.Clear(); s_PadInput.frame.down = s_PadInput.frame.up = 0; }
void Input::SetInjectedPS1Input(uint16_t held,const std::array<float,4>& axes) { s_InjectedHeld=held;s_InjectedAxes=axes; }
void Input::ClearInjectedPS1Input() { s_InjectedHeld=s_PreviousInjectedHeld=0;s_InjectedAxes={};s_InjectedUIPending=false;s_InjectedUIFrame={}; }
void Input::BeginInjectedPS1Frame() {
    s_PadInput.frame.held=s_InjectedHeld;
    s_PadInput.frame.down=s_InjectedHeld & ~s_PreviousInjectedHeld;
    s_PadInput.frame.up=s_PreviousInjectedHeld & ~s_InjectedHeld;
    s_PadInput.frame.axes=s_InjectedAxes;
    s_PreviousInjectedHeld=s_InjectedHeld;
    s_InjectedUIFrame=s_PadInput.frame;s_InjectedUIPending=true;
}

void Input::SetPS1PadMapping(const PS1PadMapping& mapping) { s_PadInput.mapping=mapping; s_PadInput.Clear(); }
void Input::SamplePS1Pad(const PS1PadSample& sample) {
    s_PadInput.Sample(sample.held,sample.down,sample.up,sample.physicalButtons,
        sample.physicalAxes,sample.mouse,sample.seconds,s_GameInputEnabled && sample.focused);
}

void Input::SetPreparationInProgress(bool preparing) {
    // Called only by the UI thread, before launch / after joining the worker.
    if (preparing) s_PreparationUIThread = std::this_thread::get_id();
    s_Preparing = preparing;
    if (!preparing) WindowFocusCallback(s_Window, 0);
}

void Input::SetCursorCaptureAllowed(bool allowed) {
    s_CursorCaptureAllowed=allowed;
    if (!allowed) SetCursorLocked(false);
}
bool Input::IsPS1Button(const std::string& name) { return PS1ButtonIndex(name)>=0 || name=="QuickTurn"; }
static const PS1PadFrame& PadFrame() { return s_UsePadFrame ? s_PadInput.frame : s_PadInput.current; }
bool Input::GetPS1Button(const std::string& name) {
    if (!s_GameInputEnabled && !(s_UsePadFrame&&s_InjectedUIPending)) return false;
    if (name=="QuickTurn") return GetPS1Button("L1") && GetPS1Button("R1");
    const int index=PS1ButtonIndex(name); return index>=0 && (PadFrame().held & (1u<<index));
}
bool Input::GetPS1ButtonDown(const std::string& name) {
    if (!s_GameInputEnabled && !(s_UsePadFrame&&s_InjectedUIPending)) return false;
    if (name=="QuickTurn") return (GetPS1ButtonDown("L1") && GetPS1Button("R1")) ||
        (GetPS1ButtonDown("R1") && GetPS1Button("L1"));
    const int index=PS1ButtonIndex(name); return index>=0 && (PadFrame().down & (1u<<index));
}
bool Input::GetPS1ButtonUp(const std::string& name) {
    if (!s_GameInputEnabled && !(s_UsePadFrame&&s_InjectedUIPending)) return false;
    if (name=="QuickTurn") return GetPS1ButtonUp("L1") || GetPS1ButtonUp("R1");
    const int index=PS1ButtonIndex(name); return index>=0 && (PadFrame().up & (1u<<index));
}
float Input::GetPS1Axis(const std::string& name) {
    if (!s_GameInputEnabled && !(s_UsePadFrame&&s_InjectedUIPending)) return 0;
    const int index=name=="LeftStickX" || name=="Horizontal" ? 0 :
        name=="LeftStickY" || name=="Vertical" ? 1 : name=="RightStickX" ? 2 : name=="RightStickY" ? 3 : -1;
    return index>=0 ? PadFrame().axes[index] : 0;
}

bool Input::IsKeyPressed(int key) {
    if (key < 0 || key > GLFW_KEY_LAST)
        return false;
    return s_PrevKeyDown[key];
}

bool Input::IsKeyDown(int key) {
    if (key < 0 || key > GLFW_KEY_LAST)
        return false;
    return s_UseGameBuffer ? s_GameFrameBuffer.keyDown[key] : s_KeyDownEdge[key];
}

bool Input::IsKeyReleased(int key) {
    if (key < 0 || key > GLFW_KEY_LAST)
        return true;
    return !s_PrevKeyDown[key];
}

bool Input::IsMouseButtonPressed(int button) {
    return glfwGetMouseButton(GetGameInputWindow(), button) == GLFW_PRESS;
}

bool Input::IsMouseButtonReleased(int button) {
    return glfwGetMouseButton(GetGameInputWindow(), button) == GLFW_RELEASE;
}

glm::vec2 Input::GetMousePosition() {
    return s_CurrentMousePos;
}

float Input::GetMouseX() { return GetMousePosition().x; }
float Input::GetMouseY() { return GetMousePosition().y; }

glm::vec2 Input::GetMouseDelta() { return s_UseGameBuffer ? s_GameFrameBuffer.mouseDelta : s_MouseDelta; }
float Input::GetScrollDelta() { return s_UseGameBuffer ? s_GameFrameBuffer.scrollDelta : s_ScrollDelta; }

} // namespace MipsyncEngine
