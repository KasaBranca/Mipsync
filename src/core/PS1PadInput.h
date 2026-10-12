#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>
#include <string>

namespace MipsyncEngine {
// Bindings use GLFW keys, 1000..1007 mouse buttons, 1100..1103 mouse motion; -1 is unbound.
// Kept independent of GLFW/ImGui so input sampling can be tested without a window.
struct PS1ControlDefinition { const char* name; const char* preference; int binding; };
inline constexpr int PS1MouseUp=1100, PS1MouseDown=1101, PS1MouseLeft=1102, PS1MouseRight=1103;
inline constexpr std::array<PS1ControlDefinition, 24> PS1Controls{{
    {"Cross", "padCross", 32}, {"Circle", "padCircle", 67},
    {"Square", "padSquare", 340}, {"Triangle", "padTriangle", 70},
    {"Start", "padStart", 257}, {"Select", "padSelect", 258},
    {"L1", "padL1", 81}, {"R1", "padR1", 69},
    {"L2", "padL2", 1001}, {"R2", "padR2", 1000},
    {"L3", "padL3", 341}, {"R3", "padR3", 1002},
    {"DPadUp", "padUp", 265}, {"DPadDown", "padDown", 264},
    {"DPadLeft", "padLeft", 263}, {"DPadRight", "padRight", 262},
    {"Left Stick Up", "padLeftStickUp", 87}, {"Left Stick Down", "padLeftStickDown", 83},
    {"Left Stick Left", "padLeftStickLeft", 65}, {"Left Stick Right", "padLeftStickRight", 68},
    {"Right Stick Up", "padRightStickUp", PS1MouseUp}, {"Right Stick Down", "padRightStickDown", PS1MouseDown},
    {"Right Stick Left", "padRightStickLeft", PS1MouseLeft}, {"Right Stick Right", "padRightStickRight", PS1MouseRight}
}};
inline int PS1ButtonIndex(std::string_view name) {
    for (int i = 0; i < 16; ++i) if (PS1Controls[i].name == name) return i;
    // Existing PS1 semantic aliases continue to work with the same bindings.
    if (name == "Jump") return 0;
    if (name == "Run") return 2;
    if (name == "Aim") return 9;
    if (name == "StrafeLeft") return 6;
    if (name == "StrafeRight") return 7;
    return -1;
}
struct PS1BindingOption { int code; std::string label; };
inline const std::vector<PS1BindingOption>& PS1BindingOptions() {
    static const auto options = [] {
        std::vector<PS1BindingOption> result{{-1,"None"},{32,"Space"},{257,"Enter"},{258,"Tab"},
            {259,"Backspace"},{260,"Insert"},{261,"Delete"},{262,"Right Arrow"},
            {263,"Left Arrow"},{264,"Down Arrow"},{265,"Up Arrow"},
            {266,"Page Up"},{267,"Page Down"},{268,"Home"},{269,"End"},
            {340,"Left Shift"},{344,"Right Shift"},{341,"Left Control"},{345,"Right Control"},
            {342,"Left Alt"},{346,"Right Alt"},
            {1000,"Mouse Left"},{1001,"Mouse Right"},{1002,"Mouse Middle"},
            {1003,"Mouse Button 4"},{1004,"Mouse Button 5"},
            {PS1MouseUp,"Mouse Up"},{PS1MouseDown,"Mouse Down"},
            {PS1MouseLeft,"Mouse Left"},{PS1MouseRight,"Mouse Right"}};
        for (int c = 'A'; c <= 'Z'; ++c) result.push_back({c,std::string(1,static_cast<char>(c))});
        for (int c = '0'; c <= '9'; ++c) result.push_back({c,std::string(1,static_cast<char>(c))});
        for (int i = 0; i < 12; ++i) result.push_back({290+i,"F"+std::to_string(i+1)});
        for (int i = 0; i < 10; ++i) result.push_back({320+i,"Numpad "+std::to_string(i)});
        for (const auto& option : std::vector<PS1BindingOption>{{39,"'"},{44,","},{45,"-"},{46,"."},{47,"/"},
            {59,";"},{61,"="},{91,"["},{92,"\\"},{93,"]"},{96,"`"},
            {330,"Numpad Decimal"},{331,"Numpad Divide"},{332,"Numpad Multiply"},
            {333,"Numpad Subtract"},{334,"Numpad Add"},{335,"Numpad Enter"}}) result.push_back(option);
        return result;
    }();
    return options;
}
struct PS1PadMapping {
    std::array<int, 24> bindings{};
    float mouseSensitivity = .003f; // stick deflection per pixel/second
    float deadzone = .125f;
    bool invertMouseX = false, invertMouseY = false;
    PS1PadMapping() { for (size_t i=0; i<bindings.size(); ++i) bindings[i]=PS1Controls[i].binding; }
};
struct PS1PadFrame {
    uint16_t held = 0, down = 0, up = 0;
    std::array<float, 4> axes{}; // right positive, up positive
};
struct PS1PadSample {
    std::array<bool,24> held{}, down{}, up{};
    uint16_t physicalButtons=0;
    std::array<float,4> physicalAxes{};
    std::array<float,2> mouse{};
    float seconds=0;
    bool focused=true;
};
class PS1PadInput {
public:
    PS1PadMapping mapping;
    PS1PadFrame current, frame;
    void Clear() { current = {}; frame = {}; m_PendingDown = m_PendingUp = 0;
        m_Mouse = {}; m_Elapsed = 0; m_BaseAxes = {}; }
    static float RemapAxis(float axis, float deadzone) {
        if (!std::isfinite(axis)) return 0;
        deadzone = std::clamp(deadzone, 0.f, .95f);
        const float magnitude = std::min(1.f, std::abs(axis));
        return magnitude <= deadzone ? 0.f : std::copysign((magnitude-deadzone)/(1-deadzone),axis);
    }
    void Sample(const std::array<bool,24>& held, const std::array<bool,24>& down,
                const std::array<bool,24>& up, uint16_t physicalButtons,
                const std::array<float,4>& physicalAxes, std::array<float,2> mouse,
                float seconds, bool enabled) {
        if (!enabled) { Clear(); return; }
        uint16_t buttons = physicalButtons, taps = 0, releases = 0;
        for (int i=0; i<16; ++i) {
            if (held[i]) buttons |= uint16_t(1u<<i);
            if (down[i]) taps |= uint16_t(1u<<i);
            if (up[i] && !(buttons & (1u<<i))) releases |= uint16_t(1u<<i);
        }
        current.down = uint16_t((buttons & ~current.held) | (taps & ~current.held));
        current.up = uint16_t((current.held & ~buttons) | releases);
        current.held = buttons;
        m_PendingDown |= current.down; m_PendingUp |= current.up;
        for (int i=0; i<4; ++i) m_BaseAxes[i] = RemapAxis(physicalAxes[i],mapping.deadzone);
        m_BaseAxes[0] += float(held[19])-float(held[18]) + float((buttons>>15)&1)-float((buttons>>14)&1);
        m_BaseAxes[1] += float(held[16])-float(held[17]) + float((buttons>>12)&1)-float((buttons>>13)&1);
        m_BaseAxes[2] += float(held[23])-float(held[22]);
        m_BaseAxes[3] += float(held[20])-float(held[21]);
        current.axes = AxesWithMouse(mouse,seconds);
        m_Mouse[0] += mouse[0]; m_Mouse[1] += mouse[1];
        m_Elapsed += std::max(0.f,seconds);
    }
    void BeginFrame() {
        frame = current; frame.down = m_PendingDown; frame.up = m_PendingUp;
        frame.axes = AxesWithMouse(m_Mouse,m_Elapsed);
    }
    void Consume() { m_PendingDown = m_PendingUp = 0; m_Mouse = {}; m_Elapsed = 0; }
private:
    uint16_t m_PendingDown = 0, m_PendingUp = 0;
    std::array<float,2> m_Mouse{};
    std::array<float,4> m_BaseAxes{};
    float m_Elapsed = 0;
    std::array<float,4> AxesWithMouse(std::array<float,2> mouse, float seconds) const {
        auto axes = m_BaseAxes;
        if (seconds > .000001f) {
            const float x=mouse[0]*mapping.mouseSensitivity/seconds*(mapping.invertMouseX ? -1 : 1);
            const float y=-mouse[1]*mapping.mouseSensitivity/seconds*(mapping.invertMouseY ? -1 : 1);
            const auto strength = [x,y](int binding) {
                if (binding==PS1MouseUp) return std::max(0.f,y);
                if (binding==PS1MouseDown) return std::max(0.f,-y);
                if (binding==PS1MouseLeft) return std::max(0.f,-x);
                if (binding==PS1MouseRight) return std::max(0.f,x);
                return 0.f;
            };
            for (int stick=0; stick<2; ++stick) {
                const int first=16+stick*4;
                float mouseX=strength(mapping.bindings[first+3])-strength(mapping.bindings[first+2]);
                float mouseY=strength(mapping.bindings[first])-strength(mapping.bindings[first+1]);
                const float magnitude=std::hypot(mouseX,mouseY);
                if (magnitude>1) { mouseX/=magnitude; mouseY/=magnitude; }
                axes[stick*2] += mouseX;
                axes[stick*2+1] += mouseY;
            }
        }
        for (auto& axis : axes) axis = std::isfinite(axis) ? std::clamp(axis,-1.f,1.f) : 0.f;
        return axes;
    }
};
} // namespace MipsyncEngine
