// SPDX-License-Identifier: BSD-3-Clause
// gow3 - ReXGlue Recompiled Project
//
// Exit Game on the main menu. The Coalesced edit (gow3_coalesced.cpp) renames the defunct "Xbox LIVE
// Marketplace" entry; selecting it makes the game look up the "NeedLiveForDownloadableContent"
// error text (sub_822FC8D0, Localize) before showing that error. That lookup ends the process
// instead, as the window's close button does in the end (ReXApp::OnClosing hard-exits). Closing the
// window from here deadlocked: the title shutdown waits for this guest thread, which goes on to show
// the error box through the UI thread.

#include "gow3_main_menu_exit.h"

#include <windows.h>

#include <cstring>

#include <rex/logging.h>
#include <rex/ppc.h>

namespace rex::glue {
namespace {

bool GuestUtf16Is(const uint8_t* base, uint32_t addr, const char* text) {
  if (!addr) return false;
  for (uint32_t i = 0;; ++i) {
    const uint16_t c = uint16_t((base[addr + 2 * i] << 8) | base[addr + 2 * i + 1]);
    if (c != uint8_t(text[i])) return false;
    if (!c) return true;
  }
}

bool MatchesNeedLive(const uint8_t* base, uint32_t addr) {
  if (!addr) return false;
  if (GuestUtf16Is(base, addr, "NeedLiveForDownloadableContent")) return true;
  const char* str = reinterpret_cast<const char*>(base + addr);
  return std::strncmp(str, "NeedLiveForDownloadableContent", 30) == 0;
}

void ExitGame() {
  REXLOG_INFO("MAINMENU: Exit Game selected, ending the process");
  rex::FlushLogging();
  TerminateProcess(GetCurrentProcess(), 0);
}

}  // namespace
}  // namespace rex::glue

extern "C" void __imp__sub_822FC8D0(PPCContext& ctx, uint8_t* base);

extern "C" void sub_822FC8D0(PPCContext& ctx, uint8_t* base) {
  if (REXCVAR_GET(gow3_main_menu_exit) && rex::glue::MatchesNeedLive(base, ctx.r5.u32)) {
    rex::glue::ExitGame();
  }
  __imp__sub_822FC8D0(ctx, base);
}
