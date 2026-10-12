// Include the input backend to feed its callback buffer without a GUI window.
#include "core/Input.h"
// A platform boundary that cannot move or grab the user's actual desktop mouse.
struct MouseWindow {
    int cursor = GLFW_CURSOR_NORMAL;
    int raw = GLFW_FALSE;
    int focused = GLFW_TRUE;
    double x = 0, y = 0;
};
static MouseWindow& MouseState(GLFWwindow* window) { return *reinterpret_cast<MouseWindow*>(window); }
static int MouseMode(GLFWwindow* window, int mode) { return mode == GLFW_CURSOR ? MouseState(window).cursor : MouseState(window).raw; }
static void SetMouseMode(GLFWwindow* window, int mode, int value) {
    if (mode == GLFW_CURSOR) MouseState(window).cursor = value;
    else MouseState(window).raw = value;
}
static int MouseAttribute(GLFWwindow* window, int) { return MouseState(window).focused; }
static void MousePosition(GLFWwindow* window, double* x, double* y) { *x=MouseState(window).x; *y=MouseState(window).y; }
static int RawMouseSupported() { return GLFW_TRUE; }
static int NoPhysicalPad(int, GLFWgamepadstate*) { return GLFW_FALSE; }
static double SampleTime() { static double time=0; return time+=1.0/60.0; }
#define glfwGetInputMode MouseMode
#define glfwSetInputMode SetMouseMode
#define glfwGetWindowAttrib MouseAttribute
#define glfwGetCursorPos MousePosition
#define glfwRawMouseMotionSupported RawMouseSupported
#define glfwGetGamepadState NoPhysicalPad
#define glfwGetTime SampleTime
#include "../src/core/Input.cpp"
#include <iostream>
#include <stdexcept>
#include "../templates/ps1/starter/runtime/pad_mapping.h"

using namespace MipsyncEngine;
static void Require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }

int main() {
    try {
        PS1PadInput pad;
        std::array<bool,24> held{}, down{}, up{};
        down[0]=true; up[0]=true; // complete tap between samples
        pad.Sample(held,down,up,0,{0,0,0,0},{0,0},1.f/60,true);
        down={}; up={};
        for (int i=0;i<3;++i) pad.Sample(held,down,up,0,{0,0,0,0},{0,0},1.f/60,true);
        pad.BeginFrame();
        Require((pad.frame.down&1) && (pad.frame.up&1) && !pad.frame.held,"PS1 short button tap was lost");
        pad.Consume(); pad.BeginFrame(); Require(!pad.frame.down && !pad.frame.up,"PS1 tap replayed");
        pad.Clear(); held[16]=true; held[19]=true;
        pad.Sample(held,down,up,0,{0,0,0,0},{0,0},1.f/60,true); pad.BeginFrame();
        Require(pad.frame.axes[0]==1 && pad.frame.axes[1]==1,"PS1 WASD stick mapping wrong");
        pad.Clear(); held={};
        pad.Sample(held,down,up,1u<<14,{0,0,0,0},{0,0},1.f/60,true); pad.BeginFrame();
        Require(pad.frame.axes[0]==-1,"PS1 D-pad did not drive movement axis");
        pad.Clear();
        for (int i=0;i<4;++i) pad.Sample(held,down,up,0,{0,0,0,0},{1,-1},1.f/60,true);
        pad.BeginFrame(); const auto slow=pad.frame.axes;
        pad.Clear(); pad.Sample(held,down,up,0,{0,0,0,0},{1,-1},1.f/60,true); pad.BeginFrame();
        Require(std::abs(pad.frame.axes[2]-slow[2])<.00001f && std::abs(pad.frame.axes[3]-slow[3])<.00001f,
            "mouse stick strength depends on P-Sync cadence");
        Require(slow[2]>0 && slow[3]>0 && slow[2]<=1,"mouse stick signs/range wrong");
        for (const auto delta : std::array<std::array<float,2>,4>{{{1,0},{-1,0},{0,-1},{0,1}}}) {
            pad.Clear(); pad.Sample(held,down,up,0,{0,0,0,0},delta,1.f/60,true); pad.BeginFrame();
            Require(pad.frame.axes[0]==0 && pad.frame.axes[1]==0 &&
                std::abs(pad.frame.axes[2]-delta[0]*.18f)<.00001f &&
                std::abs(pad.frame.axes[3]+delta[1]*.18f)<.00001f,"default mouse directions do not control right stick");
        }
        const auto defaults=pad.mapping;
        for (int i=20; i<24; ++i) pad.mapping.bindings[i]=-1;
        pad.Clear(); pad.Sample(held,down,up,0,{0,0,0,0},{10,10},1.f/60,true); pad.BeginFrame();
        Require(pad.frame.axes==std::array<float,4>{},"unbound mouse directions still drive stick");
        pad.mapping.bindings[16]=PS1MouseUp;
        pad.Clear(); pad.Sample(held,down,up,0,{0,0,0,0},{0,-1},1.f/60,true); pad.BeginFrame();
        Require(pad.frame.axes[1]>0 && pad.frame.axes[3]==0,"mouse direction remapping ignored");
        pad.mapping=defaults;
        pad.mapping.invertMouseY=true;
        pad.Clear(); pad.Sample(held,down,up,0,{0,0,0,0},{1000,-1000},1.f/60,true); pad.BeginFrame();
        Require(std::abs(pad.frame.axes[2]-.70710678f)<.00001f &&
            std::abs(pad.frame.axes[3]+.70710678f)<.00001f,"mouse inversion or circular clamping failed");
        pad.mapping.invertMouseY=false;
        pad.Clear(); pad.Sample(held,down,up,0,{0,0,0,0},{1000,-500},1.f/60,true); pad.BeginFrame();
        Require(std::abs(pad.frame.axes[2]-2*pad.frame.axes[3])<.00001f &&
            std::abs(std::hypot(pad.frame.axes[2],pad.frame.axes[3])-1)<.00001f,
            "strong diagonal mouse input changed direction or speed");
        pad.Sample(held,down,up,1,{1,1,1,1},{1,1},1.f/60,false); pad.BeginFrame();
        Require(!pad.frame.held && !pad.frame.down && pad.frame.axes==std::array<float,4>{},"inactive Game View leaked input");
        const uint16_t masks[16]={0x4000,0x2000,0x8000,0x1000,8,1,0x400,0x800,0x100,0x200,2,4,0x10,0x40,0x80,0x20};
        for (int i=0;i<16;++i) Require(ps1_named_button_mask(PS1Controls[i].name)==masks[i],"desktop and PS1 button names differ");
        Require(!ps1_named_button_mask("W") && PS1ButtonIndex("W")==-1,"PS1 API accepts PC key names");
        for (int raw=-128;raw<=127;++raw) {
            const float axis=PS1PadInput::RemapAxis(std::clamp(float(raw)/127.f,-1.f,1.f),16.f/127.f);
            Require(std::abs(axis-float(ps1_stick_axis_q16(raw))/65536.f)<.00002f,"PS1 analog axis differs from desktop");
        }
        PS1PadMapping mapping; mapping.bindings[0]=GLFW_KEY_F;
        Input::SetPS1PadMapping(mapping);
        held={}; down={}; up={};
        s_PadInput.Sample(held,down,up,0,{0,0,0,0},{1,-1},1.f/60,true);
        Input::BeginGameFrame(true);
        Require(Input::GetPS1Axis("RightStickX")>0 && Input::GetPS1Axis("RightStickY")>0 &&
            Input::GetPS1Axis("LeftStickX")==0,"VM input scope misses default right mouse stick");
        Input::EndGameFrame();
        Input::SetPS1PadMapping(mapping);
        // Feed the actual input scope used by the VM, including UI handoff.
        held={}; down={}; up={}; down[0]=true;
        s_PadInput.Sample(held,down,up,0,{0,0,0,0},{0,0},1.f/60,true);
        Input::BeginGameFrame(true); Require(Input::GetPS1ButtonDown("Cross"),"VM misses virtual PS1 press");
        Input::EndGameFrame(); Input::BeginGameUIFrame(true);
        Require(Input::GetPS1ButtonDown("Cross"),"UI lost virtual PS1 press"); Input::EndGameUIFrame();
        Input::BeginGameFrame(true); Require(!Input::GetPS1ButtonDown("Cross"),"virtual PS1 press replayed"); Input::EndGameFrame();
        Input::SetGameInputEnabled(false); Require(!Input::GetPS1ButtonDown("Cross"),"disabled pad leaked edge");
        Input::SetGameInputEnabled(true);
        Input::SetInjectedPS1Input(1,{0,0,.25f,-.5f});
        Input::BeginGameFrame(false);Input::BeginInjectedPS1Frame();
        Require(Input::GetPS1ButtonDown("Cross")&&Input::GetPS1Axis("RightStickX")==.25f,
            "injected PS1 input did not reach the gameplay frame");
        Input::EndGameFrame();Input::BeginGameFrame(false);Input::BeginInjectedPS1Frame();
        Require(Input::GetPS1Button("Cross")&&!Input::GetPS1ButtonDown("Cross"),"injected held input replayed its press edge");
        Input::EndGameFrame();Input::SetInjectedPS1Input(0,{});Input::BeginGameFrame(false);Input::BeginInjectedPS1Frame();
        Require(Input::GetPS1ButtonUp("Cross"),"injected PS1 release edge was lost");
        Input::EndGameFrame();Input::ClearInjectedPS1Input();
        bool edges[GLFW_KEY_LAST + 1] = {};
        edges[GLFW_KEY_SPACE] = true;
        s_GameBuffer.Accumulate(edges, {5, 3}, 2);
        edges[GLFW_KEY_SPACE] = false;
        s_GameBuffer.Accumulate(edges, {-2, 1}, -1);
        Input::BeginGameFrame(true);
        Require(Input::IsKeyDown(GLFW_KEY_SPACE), "script misses short press between game ticks");
        Require(Input::GetMouseDelta() == glm::vec2(3, 4) && Input::GetScrollDelta() == 1,
                "script input doesn't accumulate");
        Input::EndGameFrame();
        Require(!Input::IsKeyDown(GLFW_KEY_SPACE), "game key leaked into editor scope");
        Input::BeginGameUIFrame(true);
        Require(Input::IsKeyDown(GLFW_KEY_SPACE), "UI lost key consumed by script update");
        Input::EndGameUIFrame();
        Input::BeginGameFrame(true);
        Require(!Input::IsKeyDown(GLFW_KEY_SPACE) && Input::GetMouseDelta() == glm::vec2(0),
                "input replayed on next tick");
        Input::EndGameFrame();
        s_KeyDownEdge[GLFW_KEY_A] = true;
        Input::BeginGameFrame(false);
        Require(Input::IsKeyDown(GLFW_KEY_A), "normal mode key handling changed");
        Input::EndGameFrame();
        s_GameBuffer.Accumulate(edges, {10, 10}, 0);
        Input::BeginGameFrame(true);
        WindowFocusCallback(nullptr, 0);
        Require(Input::GetMouseDelta() == glm::vec2(0), "focus loss didn't clear pending input");
        Input::EndGameFrame();
        MouseWindow mainMouse, gameMouse;
        mainMouse.focused=GLFW_FALSE;
        auto* mainWindow=reinterpret_cast<GLFWwindow*>(&mainMouse);
        auto* gameWindow=reinterpret_cast<GLFWwindow*>(&gameMouse);
        Input::SetGameInputWindow(mainWindow);
        Input::SetCursorCaptureAllowed(false);
        Input::SetCursorLocked(true);
        Require(!Input::IsCursorLocked() && mainMouse.cursor==GLFW_CURSOR_NORMAL,
                "Play acquired mouse before the Game View was clicked");
        Input::SetGameInputWindow(gameWindow);
        Input::SetCursorCaptureAllowed(true);
        Input::SetGameInputEnabled(true);
        Input::SetCursorLocked(true);
        Require(Input::IsCursorLocked() && gameMouse.cursor==GLFW_CURSOR_DISABLED &&
                gameMouse.raw==GLFW_TRUE && mainMouse.cursor==GLFW_CURSOR_NORMAL,
                "capture hid the cursor without locking the detached Game View");
        Input::Update();Require(Input::GetMouseDelta()==glm::vec2(0),"capture introduced a mouse jump");
        gameMouse.x+=10000;gameMouse.y-=5000;
        Input::Update();Require(Input::GetMouseDelta()==glm::vec2(10000,-5000) &&
                Input::GetPS1Axis("RightStickX")>0 && Input::GetPS1Axis("RightStickY")>0,
                "relative mouse input stopped at the viewport bounds or used the main window");
        gameMouse.cursor=GLFW_CURSOR_HIDDEN;
        Require(!Input::IsCursorLocked(),"lock state ignored a platform override");
        Input::SetCursorLocked(true);
        Require(gameMouse.cursor==GLFW_CURSOR_DISABLED,"cached capture did not restore the actual lock");
        Input::Update();Require(Input::GetMouseDelta()==glm::vec2(0),"recapture introduced a mouse jump");
        Input::SetGameInputEnabled(false);
        Input::SetPreparationInProgress(true);
        std::thread script([] {
            Require(Input::IsCursorLocked(),"preparation lost the captured cursor snapshot");
            Input::SetCursorLocked(false);
        });
        script.join();
        Require(Input::IsCursorLocked() && gameMouse.cursor==GLFW_CURSOR_DISABLED &&
                Input::GetGameInputWindow()==gameWindow,
                "scene preparation changed the captured window or let a worker unlock it");
        Input::SetCursorCaptureAllowed(false);
        Require(!Input::IsCursorLocked() && gameMouse.cursor==GLFW_CURSOR_NORMAL && !gameMouse.raw,
                "UI could not release the cursor while a preparation worker ran");
        Input::SetPreparationInProgress(false);
        Input::SetPreparationInProgress(true);
        std::thread destination([] { Input::SetCursorLocked(true); });
        destination.join();
        Input::SetPreparationInProgress(false);
        Require(!Input::IsCursorLocked(),"destination Awake reacquired a released cursor");
        Input::SetGameInputEnabled(true);
        Input::Update();Require(Input::GetMouseDelta()==glm::vec2(0),"scene preparation replayed mouse movement");
        gameMouse.focused=GLFW_FALSE;mainMouse.focused=GLFW_TRUE;gameMouse.x+=1;
        Input::Update();Require(Input::GetPS1Axis("RightStickX")==0,"main-window focus enabled detached-game input");
        Input::SetCursorCaptureAllowed(false);
        Require(gameMouse.cursor==GLFW_CURSOR_NORMAL && gameMouse.raw==GLFW_FALSE,
                "release left the OS cursor captured");
        Input::SetGameInputWindow(mainWindow);Input::SetCursorCaptureAllowed(true);Input::SetCursorLocked(true);
        Input::ForgetGameInputWindow(mainWindow);
        Require(!Input::GetGameInputWindow() && !Input::IsCursorLocked() && mainMouse.cursor==GLFW_CURSOR_NORMAL,
                "destroyed viewport retained a capture or dangling input target");
        std::cout << "P-Sync input scopes, UI handoff, normal mode and focus-loss checks passed\n";
        std::cout << "Mouse capture, detached viewport routing, relative motion, recapture and release checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
