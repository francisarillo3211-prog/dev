#include "overlay.hpp"
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_dx11.h"
#include "imgui/backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace overlay {

int width = 1920, height = 1080;
static HWND  s_hwnd = nullptr;
static bool  s_run  = true;
static ID3D11Device*        dev = nullptr;
static ID3D11DeviceContext* ctx = nullptr;
static IDXGISwapChain*      sc  = nullptr;
static ID3D11RenderTargetView* rtv = nullptr;

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return true;
    if (m == WM_DESTROY) { s_run = false; return 0; }
    return DefWindowProcW(h, m, w, l);
}

static void make_rtv() {
    ID3D11Texture2D* back = nullptr;
    sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back);
    if (back) { dev->CreateRenderTargetView(back, nullptr, &rtv); back->Release(); }
}

bool create(HINSTANCE hinst, int w, int h) {
    width = GetSystemMetrics(SM_CXSCREEN);
    height = GetSystemMetrics(SM_CYSCREEN);

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hinst;
    wc.lpszClassName = L"rr_overlay";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    s_hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
                             wc.lpszClassName, L"RobRunExt", WS_POPUP,
                             0, 0, width, height, nullptr, nullptr, hinst, nullptr);
    if (!s_hwnd) return false;

    SetLayeredWindowAttributes(s_hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
    ShowWindow(s_hwnd, SW_SHOW);

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.Width = width;
    sd.BufferDesc.Height = height;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = s_hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL lvl;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            levels, 2, D3D11_SDK_VERSION, &sd, &sc, &dev, &lvl, &ctx)))
        return false;

    make_rtv();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(s_hwnd);
    ImGui_ImplDX11_Init(dev, ctx);
    return true;
}

void begin_frame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void end_frame() {
    ImGui::Render();
    const float clear[4] = { 0, 0, 0, 0 };
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->ClearRenderTargetView(rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    sc->Present(0, 0);
}

bool running() { return s_run; }
HWND hwnd()     { return s_hwnd; }

void set_visible(bool v) { ShowWindow(s_hwnd, v ? SW_SHOW : SW_HIDE); }

void set_click_through(bool v) {
    LONG_PTR ex = GetWindowLongPtrW(s_hwnd, GWL_EXSTYLE);
    ex = v ? (ex | WS_EX_TRANSPARENT) : (ex & ~WS_EX_TRANSPARENT);
    SetWindowLongPtrW(s_hwnd, GWL_EXSTYLE, ex);
}

void destroy() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    if (rtv) rtv->Release();
    if (sc)  sc->Release();
    if (ctx) ctx->Release();
    if (dev) dev->Release();
    DestroyWindow(s_hwnd);
}

} // namespace overlay