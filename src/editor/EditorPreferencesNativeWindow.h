#pragma once
#include <imgui.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <algorithm>

namespace MipsyncEngine::PreferencesNativeWindow {
inline constexpr wchar_t FrameProperty[] = L"MipsyncPreferencesFrameProc";
inline void (*OriginalSetPosition)(ImGuiViewport*, ImVec2) = nullptr;
inline void (*OriginalSetSize)(ImGuiViewport*, ImVec2) = nullptr;

inline HWND Handle(ImGuiViewport* viewport) {
    return static_cast<HWND>(viewport->PlatformHandleRaw);
}
inline bool OwnsFrame(ImGuiViewport* viewport) {
    return Handle(viewport) && GetPropW(Handle(viewport), FrameProperty);
}
inline RECT ClientInsets(HWND window) {
    RECT outer{}, client{};
    GetWindowRect(window, &outer);
    GetClientRect(window, &client);
    POINT origin{};
    ClientToScreen(window, &origin);
    return {origin.x - outer.left, origin.y - outer.top,
            outer.right - origin.x - client.right, outer.bottom - origin.y - client.bottom};
}
inline void SetPosition(ImGuiViewport* viewport, ImVec2 position) {
    if (!OwnsFrame(viewport)) { OriginalSetPosition(viewport, position); return; }
    const RECT inset = ClientInsets(Handle(viewport));
    SetWindowPos(Handle(viewport), nullptr, static_cast<int>(position.x) - inset.left,
                 static_cast<int>(position.y) - inset.top, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}
inline void SetSize(ImGuiViewport* viewport, ImVec2 size) {
    if (!OwnsFrame(viewport)) { OriginalSetSize(viewport, size); return; }
    const RECT inset = ClientInsets(Handle(viewport));
    SetWindowPos(Handle(viewport), nullptr, 0, 0,
                 static_cast<int>(size.x) + inset.left + inset.right,
                 static_cast<int>(size.y) + inset.top + inset.bottom,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}
inline LRESULT CALLBACK FrameProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    const auto original = reinterpret_cast<WNDPROC>(GetPropW(window, FrameProperty));
    if (message == WM_NCCALCSIZE && !IsZoomed(window)) {
        auto* client = wParam ? &reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0]
                             : reinterpret_cast<RECT*>(lParam);
        const RECT outer = *client;
        // Keep the OS caption, but replace its invisible resize gutters with a
        // one-pixel border. Otherwise the caption visibly overhangs the UI by 8px.
        CallWindowProcW(original, window, message, wParam, lParam);
        client->left = outer.left + 1;
        client->right = outer.right - 1;
        client->bottom = outer.bottom - 1;
        return 0;
    }
    const LRESULT result = CallWindowProcW(original, window, message, wParam, lParam);
    if (message == WM_NCHITTEST && result == HTCLIENT && !IsZoomed(window)) {
        RECT bounds{}; GetWindowRect(window, &bounds);
        const int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        const int grip = std::max(4, GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER));
        const bool left = x < bounds.left + grip, right = x >= bounds.right - grip;
        const bool top = y < bounds.top + grip, bottom = y >= bounds.bottom - grip;
        if (bottom) return left ? HTBOTTOMLEFT : right ? HTBOTTOMRIGHT : HTBOTTOM;
        if (top) return left ? HTTOPLEFT : right ? HTTOPRIGHT : HTTOP;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
    }
    if (message == WM_NCDESTROY) RemovePropW(window, FrameProperty);
    return result;
}
inline void Apply(ImGuiViewport* viewport) {
    if (viewport == ImGui::GetMainViewport()) return;
    const HWND window = Handle(viewport);
    if (!window || !IsWindow(window) || OwnsFrame(viewport)) return;
    RECT client{}; GetClientRect(window, &client);
    POINT origin{}; ClientToScreen(window, &origin);
    const auto original = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
    if (!SetPropW(window, FrameProperty, reinterpret_cast<HANDLE>(original))) return;
    SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(FrameProc));
    auto& platform = ImGui::GetPlatformIO();
    if (platform.Platform_SetWindowPos != SetPosition) {
        OriginalSetPosition = platform.Platform_SetWindowPos;
        platform.Platform_SetWindowPos = SetPosition;
    }
    if (platform.Platform_SetWindowSize != SetSize) {
        OriginalSetSize = platform.Platform_SetWindowSize;
        platform.Platform_SetWindowSize = SetSize;
    }
    SetWindowPos(window, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetSize(viewport, ImVec2(static_cast<float>(client.right), static_cast<float>(client.bottom)));
    SetPosition(viewport, ImVec2(static_cast<float>(origin.x), static_cast<float>(origin.y)));
}
} // namespace MipsyncEngine::PreferencesNativeWindow
#else
namespace MipsyncEngine::PreferencesNativeWindow {
inline void Apply(ImGuiViewport*) {}
}
#endif
