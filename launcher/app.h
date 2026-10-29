#pragma once
// The launcher's state, shared by main.cpp (window, loop, routing) and the page files.
#include <windows.h>

#include <filesystem>
#include <string>

#include "setup/state.h"

struct App {
  std::filesystem::path dir;  // launcher.exe folder
  gow3::setup::LauncherState state;
  std::filesystem::path game;  // empty if none
  enum class Page { kPlay, kGameFiles, kDisplay, kGraphics, kControls, kNetwork, kSaves } page = Page::kPlay;
  // How gow3.exe opened the launcher: --setup, --offer, --repair, or not at all (kNormal).
  enum class Start { kNormal, kSetup, kOffer, kRepair } start = Start::kNormal;
  std::wstring pass_args;  // after "--", handed back to gow3.exe
  HWND hwnd = nullptr;
  std::string error;  // last failure, shown on the Play page

  // A valid game folder and a gow3.exe next to the launcher.
  bool CanPlay() const;
  // CreateProcessW(dir/gow3.exe, pass_args), then PostQuitMessage. After a hand-over from gow3.exe
  // it adds --gow3_launcher_check=false, so the game never hands back in a loop.
  void StartGameAndQuit();
};
