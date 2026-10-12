#include "PlayLoadingProcess.h"
#include "RuntimePaths.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <atomic>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

namespace MipsyncEngine {
#ifdef _WIN32
namespace {
constexpr LONG kMagic = 0x4D504C45;
struct ProgressState {
    LONG magic;
    DWORD parentPid;
    ULONG_PTR owner;
    LONG visible;
    volatile LONG stage;
    volatile LONG closing;
    volatile LONG heartbeat;
    volatile LONG cancelRequested;
};
LONG Read(volatile LONG& value) { return InterlockedCompareExchange(&value, 0, 0); }
const wchar_t* const kStages[] = {L"Preserving scene", L"Preparing animations",
    L"Initializing Mips# scripts", L"Preparing audio", L"Preparing physics"};
struct LoadingWindow {
    ProgressState* state;
    HFONT font = nullptr;
    int tick = 0;
    float scale = 1;
    HWND cancelButton = nullptr;
    WNDPROC cancelProc = nullptr;
};
void Fill(HDC dc, RECT rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color); FillRect(dc, &rect, brush); DeleteObject(brush);
}
void Paint(HWND window, HDC target, LoadingWindow& data) {
    RECT rect; GetClientRect(window, &rect);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, rect.right, rect.bottom);
    auto previous = SelectObject(dc, bitmap);
    auto px = [&](int v) { return int(v * data.scale); };
    Fill(dc, rect, RGB(52,55,58));
    Fill(dc, {0,0,rect.right,px(29)}, RGB(61,64,67));
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(224,227,230));
    auto oldFont = SelectObject(dc, data.font);
    RECT title{px(14),px(6),rect.right-px(14),px(26)};
    DrawTextW(dc, L"Hold on...", -1, &title, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    RECT track{px(14),px(42),rect.right-px(14),px(56)};
    Fill(dc, track, RGB(37,40,43));
    const int stage = std::clamp<int>(Read(data.state->stage), 0, 4);
    RECT progress = track; progress.right = track.left+(track.right-track.left)*stage/5;
    if (progress.right > progress.left) Fill(dc, progress, RGB(13,153,255));
    // An animated activity marker, separate from completed-stage progress.
    const int length=px(22), width=track.right-track.left;
    const int x=track.left+(data.tick*px(3))%(width+length)-length;
    RECT activity{std::max<LONG>(track.left,x),track.top,std::min<LONG>(track.right,x+length),track.bottom};
    if(activity.right>activity.left) Fill(dc,activity,RGB(72,172,236));
    RECT status{px(14),px(66),rect.right-px(14),px(86)};
    DrawTextW(dc,Read(data.state->cancelRequested) ? L"Cancelling..." : kStages[stage],
              -1,&status,DT_LEFT|DT_SINGLELINE|DT_VCENTER);
    SelectObject(dc,oldFont);
    BitBlt(target,0,0,rect.right,rect.bottom,dc,0,0,SRCCOPY);
    SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
}
void RequestCancel(HWND window, LoadingWindow& data) {
    InterlockedExchange(&data.state->cancelRequested,1);
    EnableWindow(data.cancelButton,FALSE);
    InvalidateRect(window,nullptr,FALSE);
}
void PaintCancelButton(const DRAWITEMSTRUCT& item, LoadingWindow& data) {
    const RECT rect=item.rcItem;
    const bool pressed=(item.itemState&ODS_SELECTED)!=0;
    const bool disabled=(item.itemState&ODS_DISABLED)!=0;
    const int saved=SaveDC(item.hDC);
    Fill(item.hDC,rect,RGB(52,55,58));
    HRGN clip=CreateRoundRectRgn(rect.left,rect.top,rect.right,rect.bottom,
                               int(6*data.scale),int(6*data.scale));
    // WM_PRINT can provide a parent DC whose origin is translated to this
    // control; regions use device coordinates instead of that logical origin.
    POINT origin{0,0};LPtoDP(item.hDC,&origin,1);OffsetRgn(clip,origin.x,origin.y);
    ExtSelectClipRgn(item.hDC,clip,RGN_AND);
    for(int y=rect.top;y<rect.bottom;++y) {
        const int shade=pressed?54:84-(y-rect.top)*27/std::max(1L,rect.bottom-rect.top);
        Fill(item.hDC,{rect.left,y,rect.right,y+1},RGB(shade,shade+3,shade+6));
    }
    HPEN rim=CreatePen(PS_SOLID,1,RGB(34,37,40));auto previous=SelectObject(item.hDC,rim);
    auto brush=SelectObject(item.hDC,GetStockObject(HOLLOW_BRUSH));
    RoundRect(item.hDC,rect.left,rect.top,rect.right,rect.bottom,int(6*data.scale),int(6*data.scale));
    SelectObject(item.hDC,brush);SelectObject(item.hDC,previous);DeleteObject(rim);
    SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,disabled?RGB(139,142,145):RGB(224,227,230));
    SelectObject(item.hDC,data.font);RECT text=rect;
    DrawTextW(item.hDC,L"Cancel",-1,&text,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    RestoreDC(item.hDC,saved);DeleteObject(clip);
}
LRESULT CALLBACK CancelButtonProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* data=reinterpret_cast<LoadingWindow*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (data && (message==WM_PAINT || message==WM_PRINTCLIENT || message==WM_PRINT)) {
        PAINTSTRUCT paint{};
        HDC dc=message==WM_PAINT?BeginPaint(window,&paint):reinterpret_cast<HDC>(wParam);
        DRAWITEMSTRUCT item{};item.hDC=dc;GetClientRect(window,&item.rcItem);
        if(!IsWindowEnabled(window))item.itemState|=ODS_DISABLED;
        if(SendMessageW(window,BM_GETSTATE,0,0)&BST_PUSHED)item.itemState|=ODS_SELECTED;
        PaintCancelButton(item,*data);
        if(message==WM_PAINT)EndPaint(window,&paint);
        return 0;
    }
    if(message==WM_ERASEBKGND)return 1;
    return data&&data->cancelProc?CallWindowProcW(data->cancelProc,window,message,wParam,lParam)
                                  :DefWindowProcW(window,message,wParam,lParam);
}
LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    auto* data=reinterpret_cast<LoadingWindow*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {
        data=static_cast<LoadingWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(data));
    }
    switch(message) {
    case WM_CLOSE: if(data)RequestCancel(window,*data);return 0;
    case WM_KEYDOWN: if(data&&wParam==VK_ESCAPE)RequestCancel(window,*data);return 0;
    case WM_COMMAND: if(data&&LOWORD(wParam)==1&&HIWORD(wParam)==BN_CLICKED)RequestCancel(window,*data);return 0;
    case WM_DRAWITEM: if(data&&wParam==1){PaintCancelButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam),*data);return TRUE;}break;
    case WM_ERASEBKGND: return 1;
    case WM_TIMER: if(data) {++data->tick;InterlockedIncrement(&data->state->heartbeat);
        if(Read(data->state->cancelRequested))EnableWindow(data->cancelButton,FALSE);
        InvalidateRect(window,nullptr,FALSE);} return 0;
    case WM_PAINT: if(data) {PAINTSTRUCT paint;auto dc=BeginPaint(window,&paint);Paint(window,dc,*data);EndPaint(window,&paint);} return 0;
    case WM_PRINTCLIENT: if(data)Paint(window,reinterpret_cast<HDC>(wParam),*data);return 0;
    case WM_DESTROY: PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
}
struct PlayLoadingProcess::Impl {
    HANDLE mapping=nullptr, process=nullptr;
    ProgressState* state=nullptr;
    DWORD pid=0;
};
#else
struct PlayLoadingProcess::Impl {};
#endif
PlayLoadingProcess::PlayLoadingProcess():m_Impl(std::make_unique<Impl>()){}
PlayLoadingProcess::~PlayLoadingProcess(){Stop();}
bool PlayLoadingProcess::Start(GLFWwindow* owner) {
    Stop();
#ifdef _WIN32
    static std::atomic<unsigned> sequence{0};
    std::wstring name=L"Local\\Mipsync.PlayLoading."+std::to_wstring(GetCurrentProcessId())+L"."+
        std::to_wstring(GetTickCount64())+L"."+std::to_wstring(++sequence);
    m_Impl->mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(ProgressState),name.c_str());
    if(!m_Impl->mapping)return false;
    m_Impl->state=static_cast<ProgressState*>(MapViewOfFile(m_Impl->mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(ProgressState)));
    if(!m_Impl->state){Stop();return false;}
    *m_Impl->state={kMagic,GetCurrentProcessId(),reinterpret_cast<ULONG_PTR>(glfwGetWin32Window(owner)),
        glfwGetWindowAttrib(owner,GLFW_VISIBLE),0,0,0,0};
    const auto executable=GetExecutablePath();
    std::wstring command=L"\""+executable.wstring()+L"\" --editor-play-loading "+name;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,
                       nullptr,nullptr,&startup,&process)){Stop();return false;}
    CloseHandle(process.hThread);m_Impl->process=process.hProcess;m_Impl->pid=process.dwProcessId;
    return true;
#else
    (void)owner;return false;
#endif
}
void PlayLoadingProcess::SetStage(int stage) {
#ifdef _WIN32
    if(m_Impl->state)InterlockedExchange(&m_Impl->state->stage,std::clamp(stage,0,4));
#else
    (void)stage;
#endif
}
void PlayLoadingProcess::RequestCancel() {
#ifdef _WIN32
    if(m_Impl->state)InterlockedExchange(&m_Impl->state->cancelRequested,1);
#endif
}
bool PlayLoadingProcess::CancelRequested() const {
#ifdef _WIN32
    return m_Impl->state&&Read(m_Impl->state->cancelRequested)!=0;
#else
    return false;
#endif
}
bool PlayLoadingProcess::Running() const {
#ifdef _WIN32
    return m_Impl->process&&WaitForSingleObject(m_Impl->process,0)==WAIT_TIMEOUT;
#else
    return false;
#endif
}
uint32_t PlayLoadingProcess::ProcessId() const {
#ifdef _WIN32
    return m_Impl->pid;
#else
    return 0;
#endif
}
void PlayLoadingProcess::Stop() {
#ifdef _WIN32
    if(m_Impl->state)InterlockedExchange(&m_Impl->state->closing,1);
    if(m_Impl->process) {
        const auto deadline=GetTickCount64()+2000;
        while(WaitForSingleObject(m_Impl->process,0)==WAIT_TIMEOUT&&GetTickCount64()<deadline) {
            // The child owns a window of this process: keep dispatching messages
            // during its destruction instead of waiting with a blocked owner.
            MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
            Sleep(5);
        }
        if(WaitForSingleObject(m_Impl->process,0)==WAIT_TIMEOUT)TerminateProcess(m_Impl->process,1);
        CloseHandle(m_Impl->process);
    }
    if(m_Impl->state)UnmapViewOfFile(m_Impl->state);
    if(m_Impl->mapping)CloseHandle(m_Impl->mapping);
    *m_Impl={};
#endif
}
int RunPlayLoadingProcess(const std::string& mappingName) {
#ifdef _WIN32
    // Names consist solely of the generated ASCII local-session identifier.
    if(mappingName.rfind("Local\\Mipsync.PlayLoading.",0)!=0)return 2;
    std::wstring name(mappingName.begin(),mappingName.end());
    HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name.c_str());
    if(!mapping)return 2;
    auto* state=static_cast<ProgressState*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(ProgressState)));
    if(!state){CloseHandle(mapping);return 2;}
    HWND owner=reinterpret_cast<HWND>(state->owner);DWORD ownerPid=0;
    GetWindowThreadProcessId(owner,&ownerPid);
    HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,state->parentPid);
    if(state->magic!=kMagic||!parent||ownerPid!=state->parentPid) {
        if(parent)CloseHandle(parent);UnmapViewOfFile(state);CloseHandle(mapping);return 2;
    }
    SetProcessDPIAware();
    WNDCLASSW wc{};wc.lpfnWndProc=WindowProc;wc.hInstance=GetModuleHandleW(nullptr);
    wc.lpszClassName=L"MipsyncPlayLoading";wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
    RegisterClassW(&wc);
    LoadingWindow data{state};
    HDC dc=GetDC(owner);data.scale=GetDeviceCaps(dc,LOGPIXELSX)/96.0f;ReleaseDC(owner,dc);
    data.font=CreateFontW(-int(12*data.scale),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    RECT parentRect;GetWindowRect(owner,&parentRect);
    const int width=int(520*data.scale),height=int(138*data.scale);
    HWND window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"Hold on...",
        WS_POPUP|WS_BORDER|WS_CLIPCHILDREN,(parentRect.left+parentRect.right-width)/2,(parentRect.top+parentRect.bottom-height)/2,
        width,height,owner,nullptr,wc.hInstance,&data);
    if(window) {
        RECT client;GetClientRect(window,&client);
        data.cancelButton=CreateWindowExW(0,L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            client.right-int(110*data.scale),int(98*data.scale),int(96*data.scale),int(26*data.scale),
            window,reinterpret_cast<HMENU>(1),wc.hInstance,nullptr);
        // Retain the standard button's keyboard/click/accessibility behavior,
        // but own every paint path so its system border cannot reappear.
        SetWindowLongPtrW(data.cancelButton,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&data));
        data.cancelProc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(data.cancelButton,GWLP_WNDPROC,
                                                                   reinterpret_cast<LONG_PTR>(CancelButtonProc)));
        SendMessageW(data.cancelButton,WM_SETFONT,reinterpret_cast<WPARAM>(data.font),TRUE);
        SetTimer(window,1,25,nullptr);
        bool shown=false;
        while(!Read(state->closing)&&WaitForSingleObject(parent,0)==WAIT_TIMEOUT) {
            MSG message;bool quit=false;
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                if(message.message==WM_QUIT){quit=true;break;}
                TranslateMessage(&message);DispatchMessageW(&message);
            }
            if(quit)break;
            const bool show=state->visible&&IsWindowVisible(owner)&&!IsIconic(owner);
            if(show!=shown){ShowWindow(window,show?SW_SHOWNOACTIVATE:SW_HIDE);shown=show;}
            MsgWaitForMultipleObjects(0,nullptr,FALSE,20,QS_ALLINPUT);
        }
        DestroyWindow(window);
    }
    DeleteObject(data.font);CloseHandle(parent);UnmapViewOfFile(state);CloseHandle(mapping);
    return window?0:2;
#else
    (void)mappingName;return 2;
#endif
}
} // namespace MipsyncEngine
