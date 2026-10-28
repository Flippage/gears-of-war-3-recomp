// gow3 - ReXGlue Recompiled Project
//
// The renderer choice, see gow3_renderer.h.

#include "gow3_renderer.h"

#include <string>

#include <windows.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(gow3_renderer, "native", "GPU",
                      "Renderer: native (host render targets with native surfaces and resolves, "
                      "the fastest), rtv (host render targets), rov (EDRAM in the pixel shader, the "
                      "most exact and the slowest); restart")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rex::glue {
namespace {

// PCI vendor of the first display adapter (0x1002 AMD, 0x10DE NVIDIA), 0 if unknown.
uint32_t PrimaryAdapterVendor() {
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  DXGI_ADAPTER_DESC1 desc = {};
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
      FAILED(factory->EnumAdapters1(0, &adapter)) || FAILED(adapter->GetDesc1(&desc))) {
    return 0;
  }
  return desc.VendorId;
}

// Proton / Wine (Steam Deck, Linux): Direct3D 12 through vkd3d-proton, not the Windows driver.
bool RunningUnderWine() {
  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  return ntdll && GetProcAddress(ntdll, "wine_get_version") != nullptr;
}

}  // namespace

void ApplyRendererChoice() {
  // Issue #20: the native renderer showed a black screen on an AMD RX 7900 XT under the Windows
  // driver (ROV worked; a Steam Deck through Proton is fine). Until that is understood, AMD on
  // Windows starts on ROV when the player has not picked a renderer.
  if (rex::cvar::GetFlagSource("gow3_renderer") == rex::cvar::Source::kDefault &&
      PrimaryAdapterVendor() == 0x1002 && !RunningUnderWine()) {
    rex::cvar::SetFlagByName("gow3_renderer", "rov");
    REXLOG_INFO("Renderer: AMD GPU on Windows, ROV by default (issue #20); PC Options -> Graphics "
                "-> Renderer can still pick Native");
  }
  const std::string choice = REXCVAR_GET(gow3_renderer);
  const bool native = choice != "rtv" && choice != "rov";
  // The plugin registers these when it loads; set before that, they become its defaults (SDK
  // patch 0071), and gow3.toml or the command line still override them.
  rex::cvar::SetFlagByName("render_target_path_d3d12", choice == "rov" ? "rov" : "rtv");
  rex::cvar::SetFlagByName("gpu_native_surfaces", native ? "true" : "false");
  rex::cvar::SetFlagByName("gpu_native_resolves", native ? "true" : "false");
  // The HUD reaches the upscaler no more (gow2_hud_separate); the old UI mask, which let parts of
  // the scene fall back to the raw frame, stays off on the frames without the HUD-free copy too.
  rex::cvar::SetFlagByName("gow2_ui_mask", "false");
  REXLOG_INFO("Renderer: {}", native ? "native" : choice);
}

}  // namespace rex::glue
