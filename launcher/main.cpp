// launcher.exe: window, DX11, the ImGui frame loop and page routing. Pages live in pages_*.cpp,
// widgets and colours in theme.cpp.
//
//   launcher.exe [--setup | --offer | --repair] [-- <gow3.exe arguments>]
//
// gow3.exe starts it with one of the three flags when setup is needed (they open Game files); with
// none it opens Play. Everything after "--" is handed back to gow3.exe.

#include <windows.h>

#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include <imgui.h>

#include "app.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "pages_game.h"
#include "setup/finder.h"
#include "setup/state.h"
#include "theme.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

constexpr int kWidth = 900, kHeight = 560;

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swap = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
UINT g_resize_w = 0, g_resize_h = 0;  // set by WM_SIZE, applied before the next frame
UINT g_new_dpi = 0;                   // set by WM_DPICHANGED

std::filesystem::path ExeFolder() {
  std::wstring buf(MAX_PATH, L'\0');
  for (;;) {
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n < buf.size()) return std::filesystem::path(buf.substr(0, n)).parent_path();
    buf.resize(buf.size() * 2);
  }
}

// Reads the flags, and keeps everything after "--" as typed (quotes and all).
void ParseArguments(const wchar_t* p, App& app) {
  // The program name: quoted up to the next quote, else up to white space.
  if (*p == L'"') {
    ++p;
    while (*p && *p != L'"') ++p;
    if (*p) ++p;
  } else {
    while (*p && *p != L' ' && *p != L'\t') ++p;
  }
  for (;;) {
    while (*p == L' ' || *p == L'\t') ++p;
    if (!*p) return;
    const wchar_t* start = p;
    bool quoted = false;
    while (*p && (quoted || (*p != L' ' && *p != L'\t'))) {
      if (*p == L'\\' && p[1] == L'"') ++p;
      else if (*p == L'"') quoted = !quoted;
      ++p;
    }
    const std::wstring_view arg(start, p - start);
    if (arg == L"--") {
      while (*p == L' ' || *p == L'\t') ++p;
      app.pass_args = p;
      return;
    }
    if (arg == L"--setup") app.start = App::Start::kSetup;
    if (arg == L"--offer") app.start = App::Start::kOffer;
    if (arg == L"--repair") app.start = App::Start::kRepair;
  }
}

void CreateRenderTarget() {
  ID3D11Texture2D* back = nullptr;
  if (SUCCEEDED(g_swap->GetBuffer(0, IID_PPV_ARGS(&back)))) {
    g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
  }
}

void ReleaseRenderTarget() {
  if (g_rtv) g_rtv->Release();
  g_rtv = nullptr;
}

bool CreateDevice(HWND hwnd) {
  DXGI_SWAP_CHAIN_DESC sd{};
  sd.BufferCount = 2;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
  D3D_FEATURE_LEVEL level;
  HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                             D3D11_SDK_VERSION, &sd, &g_swap, &g_device, &level, &g_context);
  if (FAILED(hr)) {  // no usable GPU driver: software rendering is plenty for the launcher
    hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                       &sd, &g_swap, &g_device, &level, &g_context);
  }
  if (FAILED(hr)) return false;
  CreateRenderTarget();
  return true;
}

void ReleaseDevice() {
  ReleaseRenderTarget();
  if (g_swap) g_swap->Release();
  if (g_context) g_context->Release();
  if (g_device) g_device->Release();
  g_swap = nullptr;
  g_context = nullptr;
  g_device = nullptr;
}

// Present / ResizeBuffers results after a driver update, a TDR or a removed GPU.
bool DeviceLost(HRESULT hr) { return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET; }

// Rebuilds the device, swap chain, render target and ImGui's DX11 backend (the ImGui context and
// fonts stay), at most once a second. On failure it tells the player and returns false: quit.
bool RecreateDevice(HWND hwnd) {
  static ULONGLONG last = 0;
  if (const ULONGLONG since = GetTickCount64() - last; last && since < 1000) {
    Sleep(static_cast<DWORD>(1000 - since));
  }
  last = GetTickCount64();
  ImGui_ImplDX11_Shutdown();
  ReleaseDevice();
  g_resize_w = g_resize_h = 0;  // the new swap chain takes the window's size
  if (CreateDevice(hwnd) && ImGui_ImplDX11_Init(g_device, g_context)) return true;
  ReleaseDevice();
  MessageBoxW(hwnd, L"The graphics device was lost. Close the launcher and open it again.",
              L"Gears of War 3 Launcher", MB_ICONERROR);
  return false;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) return 1;
  switch (msg) {
    case WM_SIZE:
      if (wparam != SIZE_MINIMIZED) {
        g_resize_w = LOWORD(lparam);
        g_resize_h = HIWORD(lparam);
      }
      return 0;
    case WM_DPICHANGED: {
      g_new_dpi = HIWORD(wparam);
      const RECT* r = reinterpret_cast<const RECT*>(lparam);
      SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_SYSCOMMAND:
      if ((wparam & 0xFFF0) == SC_KEYMENU) return 0;  // Alt alone would open a window menu
      break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

// 900 x 560 times the DPI scale, centred on the monitor the window is on.
void PlaceWindow(HWND hwnd, float scale) {
  MONITORINFO mi{sizeof(mi)};
  GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
  const RECT& wa = mi.rcWork;
  const int w = static_cast<int>(kWidth * scale + 0.5f), h = static_cast<int>(kHeight * scale + 0.5f);
  SetWindowPos(hwnd, nullptr, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h,
               SWP_NOZORDER | SWP_NOACTIVATE);
}

void DrawPlaceholderPage(const char* title) { PageTitle(title); }

void DrawFrame(App& app) {
  using namespace theme;
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->Pos);
  ImGui::SetNextWindowSize(vp->Size);
  ImGui::Begin("##root", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);

  // Sidebar.
  const float sidebar_w = Dp(176.0f);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, kSidebar);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Dp(14.0f), Dp(18.0f)));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, Dp(4.0f)));
  ImGui::BeginChild("##sidebar", ImVec2(sidebar_w, 0), ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_NoScrollbar);
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + Dp(8.0f));
  ImGui::PushFont(HeadingFont(), 32.0f);
  ImGui::TextColored(kTitle, "GEARS 3");
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, Dp(10.0f)));
  using P = App::Page;
  auto nav = [&](const char* label, P page) {
    if (NavItem(label, app.page == page)) app.page = page;
  };
  nav("Play", P::kPlay);
  nav("Game files", P::kGameFiles);
  {
    ImGui::Dummy(ImVec2(0, Dp(2.0f)));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y),
                                        ImGui::GetColorU32(kLine), Dp(1.0f));
    ImGui::Dummy(ImVec2(0, Dp(2.0f)));
  }
  nav("Display", P::kDisplay);
  nav("Graphics", P::kGraphics);
  nav("Controls", P::kControls);
  nav("Network", P::kNetwork);
  nav("Saves", P::kSaves);
  ImGui::EndChild();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
  {
    const ImVec2 p = ImGui::GetItemRectMin(), q = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(q.x - Dp(0.5f), p.y), ImVec2(q.x - Dp(0.5f), q.y),
                                        ImGui::GetColorU32(kLine), Dp(1.0f));
  }

  // Page.
  ImGui::SameLine(0, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Dp(32.0f), Dp(28.0f)));
  ImGui::BeginChild("##page", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  switch (app.page) {
    case P::kPlay: DrawPlayPage(app); break;
    case P::kGameFiles: DrawPlaceholderPage("GAME FILES"); break;
    case P::kDisplay: DrawPlaceholderPage("DISPLAY"); break;
    case P::kGraphics: DrawPlaceholderPage("GRAPHICS"); break;
    case P::kControls: DrawPlaceholderPage("CONTROLS"); break;
    case P::kNetwork: DrawPlaceholderPage("NETWORK"); break;
    case P::kSaves: DrawPlaceholderPage("SAVES"); break;
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::End();

  TitleBar(app.hwnd);
}

}  // namespace

bool App::CanPlay() const {
  std::error_code ec;
  return !game.empty() && std::filesystem::is_regular_file(dir / "gow3.exe", ec);
}

void App::StartGameAndQuit() {
  const std::filesystem::path exe = dir / "gow3.exe";
  std::wstring command = L"\"" + exe.wstring() + L"\"";
  if (!pass_args.empty()) command += L" " + pass_args;
  if (start != Start::kNormal) command += L" --gow3_launcher_check=false";
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
    error = "Starting gow3.exe failed (error " + std::to_string(GetLastError()) + ").";
    return;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  PostQuitMessage(0);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  using namespace gow3::setup;
  App app;
  app.dir = ExeFolder();
  std::error_code ec;
  std::filesystem::remove(app.dir / "launcher.old.exe", ec);  // left behind by a release update
  ParseArguments(GetCommandLineW(), app);
  if (app.start != App::Start::kNormal) app.page = App::Page::kGameFiles;
  // A v0.2.2 install has no launcher.ini: LoadState's defaults (build = base) apply and the path
  // file is kept as it is.
  app.state = LoadState(app.dir / "launcher.ini");
  if (const std::filesystem::path game = ReadGamePathFile(app.dir / "gow3_game_path.txt");
      !game.empty() && IsGameFolder(game)) {
    app.game = game;
  }

  WNDCLASSEXW wc{sizeof(wc)};
  wc.style = CS_CLASSDC;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
  wc.lpszClassName = L"Gow3Launcher";
  RegisterClassExW(&wc);
  app.hwnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"Gears of War 3 Launcher",
                             WS_POPUP | WS_MINIMIZEBOX, 0, 0, kWidth, kHeight, nullptr, nullptr, instance, nullptr);
  if (!app.hwnd) return 1;
  float scale = GetDpiForWindow(app.hwnd) / 96.0f;
  PlaceWindow(app.hwnd, scale);
  const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;  // Windows 11; ignored before
  DwmSetWindowAttribute(app.hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
  if (!CreateDevice(app.hwnd)) {
    MessageBoxW(app.hwnd, L"Direct3D 11 could not be started.", L"Gears of War 3 Launcher", MB_ICONERROR);
    DestroyWindow(app.hwnd);
    return 1;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;
  ImGui_ImplWin32_Init(app.hwnd);
  ImGui_ImplDX11_Init(g_device, g_context);
  LoadFonts(app.dir);
  ApplyTheme(scale);
  RefreshPlayPage(app);

  ShowWindow(app.hwnd, show);
  UpdateWindow(app.hwnd);
  bool occluded = false;
  for (;;) {
    MSG msg;
    bool quit = false;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
      if (msg.message == WM_QUIT) quit = true;
    }
    if (quit) break;
    if ((occluded && g_swap->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) || IsIconic(app.hwnd)) {
      Sleep(10);
      continue;
    }
    occluded = false;
    if (g_new_dpi) {
      scale = g_new_dpi / 96.0f;
      g_new_dpi = 0;
      ApplyTheme(scale);
    }
    if (g_resize_w && g_resize_h) {
      ReleaseRenderTarget();
      const HRESULT hr = g_swap->ResizeBuffers(0, g_resize_w, g_resize_h, DXGI_FORMAT_UNKNOWN, 0);
      if (SUCCEEDED(hr)) {
        g_resize_w = g_resize_h = 0;
        CreateRenderTarget();
      } else if (DeviceLost(hr)) {
        if (!RecreateDevice(app.hwnd)) break;
        continue;
      }
    }
    if (!g_rtv) {  // the resize failed: try it again in a moment
      RECT rc{};
      GetClientRect(app.hwnd, &rc);
      g_resize_w = rc.right;
      g_resize_h = rc.bottom;
      Sleep(100);
      continue;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    DrawFrame(app);
    ImGui::Render();
    const ImVec4 bg = theme::kBackground;
    const float clear[4] = {bg.x, bg.y, bg.z, 1.0f};
    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_context->ClearRenderTargetView(g_rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    const HRESULT hr = g_swap->Present(1, 0);
    if (DeviceLost(hr) && !RecreateDevice(app.hwnd)) break;
    occluded = hr == DXGI_STATUS_OCCLUDED;
  }

  if (g_device) ImGui_ImplDX11_Shutdown();  // already shut down when the device couldn't be rebuilt
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  ReleaseDevice();
  if (IsWindow(app.hwnd)) DestroyWindow(app.hwnd);
  UnregisterClassW(wc.lpszClassName, instance);
  return 0;
}
