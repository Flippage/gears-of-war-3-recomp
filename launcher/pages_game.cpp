#include "pages_game.h"

#include <shobjidl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <deque>
#include <initializer_list>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "app.h"
#include "setup/finder.h"
#include "setup/install.h"
#include "setup/launch_decision.h"
#include "setup/path_util.h"
#include "setup/settings_store.h"
#include "setup/state.h"
#include "setup/stfs.h"
#include "setup/xdvdfs.h"
#include "setup/xex_info.h"
#include "theme.h"
#include "update.h"

namespace fs = std::filesystem;
using gow3::setup::Build;
using gow3::setup::ExtractResult;
using gow3::setup::FoundIso;
using gow3::setup::FoundPackage;
using gow3::setup::Progress;
using gow3::setup::SwapResult;

// ---------------------------------------------------------------------------------------------
// Job

void Job::Start(Work work) {
  if (thread_.joinable()) {  // callers check !Running(); never block on a live worker
    thread_.request_stop();
    thread_.join();
  }
  done_ = 0;
  total_ = 0;
  finished_ = false;
  cancelled_ = false;
  {
    std::lock_guard lock(mutex_);
    current_.clear();
    error_.clear();
  }
  running_ = true;
  thread_ = std::jthread([this, work = std::move(work)](std::stop_token stop) {
    const Progress progress = [this, stop](uint64_t done, uint64_t total, const std::string& current) {
      done_ = done;
      total_ = total;
      {
        std::lock_guard lock(mutex_);
        current_ = current;
      }
      return !stop.stop_requested();
    };
    std::string error;
    const ExtractResult r = work(progress, &error);
    {
      std::lock_guard lock(mutex_);
      result_ = r;
      error_ = std::move(error);
    }
    running_ = false;
    finished_ = true;
  });
}

std::string Job::Current() const {
  std::lock_guard lock(mutex_);
  return current_;
}

std::optional<ExtractResult> Job::Finished(std::string* error) {
  if (!finished_.exchange(false)) return std::nullopt;
  if (thread_.joinable()) thread_.join();
  std::lock_guard lock(mutex_);
  if (error) *error = error_;
  return result_;
}

void Job::Stop() {
  thread_.request_stop();
  if (thread_.joinable()) thread_.join();
}

// ---------------------------------------------------------------------------------------------
// File pickers

namespace {

template <typename T>
struct ComPtr {
  T* p = nullptr;
  ~ComPtr() {
    if (p) p->Release();
  }
  T* operator->() const { return p; }
  T** operator&() { return &p; }
};

fs::path ItemPath(IShellItem* item) {
  fs::path out;
  PWSTR name = nullptr;
  if (item && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) {
    out = name;
    CoTaskMemFree(name);
  }
  return out;
}

// The nearest folder of `p` (itself or a parent) that exists; empty if none.
fs::path ExistingAncestor(fs::path p) {
  std::error_code ec;
  while (!p.empty()) {
    if (fs::is_directory(p, ec)) return p;
    if (p == p.parent_path()) break;
    p = p.parent_path();
  }
  return {};
}

void SetStartFolder(IFileDialog* dialog, const fs::path& start) {
  const fs::path dir = ExistingAncestor(start);
  if (dir.empty()) return;
  ComPtr<IShellItem> item;
  if (SUCCEEDED(SHCreateItemFromParsingName(dir.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
    dialog->SetFolder(item.p);
  }
}

constexpr DWORD kUseFolderButton = 1;

// Handles the "Use this folder" button: remembers the folder being shown and closes the dialog.
class PickerEvents final : public IFileDialogEvents, public IFileDialogControlEvents {
 public:
  fs::path folder;

  IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IFileDialogEvents)) {
      *out = static_cast<IFileDialogEvents*>(this);
    } else if (riid == __uuidof(IFileDialogControlEvents)) {
      *out = static_cast<IFileDialogControlEvents*>(this);
    } else {
      *out = nullptr;
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }
  IFACEMETHODIMP_(ULONG) AddRef() override { return 2; }  // lives on the caller's stack
  IFACEMETHODIMP_(ULONG) Release() override { return 1; }

  IFACEMETHODIMP OnFileOk(IFileDialog*) override { return S_OK; }
  IFACEMETHODIMP OnFolderChanging(IFileDialog*, IShellItem*) override { return S_OK; }
  IFACEMETHODIMP OnFolderChange(IFileDialog*) override { return S_OK; }
  IFACEMETHODIMP OnSelectionChange(IFileDialog*) override { return S_OK; }
  IFACEMETHODIMP OnShareViolation(IFileDialog*, IShellItem*, FDE_SHAREVIOLATION_RESPONSE*) override {
    return S_OK;
  }
  IFACEMETHODIMP OnTypeChange(IFileDialog*) override { return S_OK; }
  IFACEMETHODIMP OnOverwrite(IFileDialog*, IShellItem*, FDE_OVERWRITE_RESPONSE*) override { return S_OK; }

  IFACEMETHODIMP OnItemSelected(IFileDialogCustomize*, DWORD, DWORD) override { return S_OK; }
  IFACEMETHODIMP OnButtonClicked(IFileDialogCustomize* customize, DWORD id) override {
    if (id != kUseFolderButton) return S_OK;
    ComPtr<IFileDialog> dialog;
    if (FAILED(customize->QueryInterface(IID_PPV_ARGS(&dialog)))) return S_OK;
    ComPtr<IShellItem> item;
    if (SUCCEEDED(dialog->GetFolder(&item))) folder = ItemPath(item.p);
    if (!folder.empty()) dialog->Close(HRESULT_FROM_WIN32(ERROR_CANCELLED));
    return S_OK;
  }
  IFACEMETHODIMP OnCheckButtonToggled(IFileDialogCustomize*, DWORD, BOOL) override { return S_OK; }
  IFACEMETHODIMP OnControlActivating(IFileDialogCustomize*, DWORD) override { return S_OK; }
};

}  // namespace

fs::path PickFolder(HWND owner, const fs::path& start) {
  ComPtr<IFileOpenDialog> dialog;
  if (FAILED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
    return {};
  }
  DWORD options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  dialog->SetTitle(L"Extract Gears of War 3 to");
  SetStartFolder(dialog.p, start);
  if (FAILED(dialog->Show(owner))) return {};
  ComPtr<IShellItem> item;
  if (FAILED(dialog->GetResult(&item))) return {};
  return ItemPath(item.p);
}

fs::path PickIsoOrFolder(HWND owner) {
  ComPtr<IFileOpenDialog> dialog;
  if (FAILED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
    return {};
  }
  DWORD options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
  dialog->SetTitle(L"Pick your Gears of War 3 ISO or game folder");
  const COMDLG_FILTERSPEC filters[] = {{L"Disc image (*.iso)", L"*.iso"}};
  dialog->SetFileTypes(1, filters);
  PickerEvents events;
  ComPtr<IFileDialogCustomize> customize;
  if (SUCCEEDED(dialog->QueryInterface(IID_PPV_ARGS(&customize)))) {
    customize->AddPushButton(kUseFolderButton, L"Use this folder");
  }
  DWORD cookie = 0;
  const bool advised = SUCCEEDED(dialog->Advise(&events, &cookie));
  const HRESULT hr = dialog->Show(owner);
  if (advised) dialog->Unadvise(cookie);
  if (!events.folder.empty()) return events.folder;
  if (FAILED(hr)) return {};
  ComPtr<IShellItem> item;
  if (FAILED(dialog->GetResult(&item))) return {};
  return ItemPath(item.p);
}

// ---------------------------------------------------------------------------------------------
// Shared helpers

namespace {

constexpr const char* kDot = " \xC2\xB7 ";  // " · "
constexpr const char* kCloseFirst = "Close Gears of War 3 first, then try again.";
constexpr const char* kNoteSaves = "Your saves carry over. The base game can't read saves made after this.";

std::string U8(const fs::path& p) { return gow3::setup::Utf8(p); }

std::string Gb(uint64_t bytes) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.1f", bytes / (1024.0 * 1024.0 * 1024.0));
  return buf;
}

std::string Ids(uint32_t title_id, uint32_t media_id) {
  return "Title ID " + gow3::setup::Hex8(title_id) + kDot + "Media ID " + gow3::setup::Hex8(media_id);
}

// Display names of the Gears 3 DLC packages in <game>\DLC, sorted.
std::vector<std::string> InstalledDlc(const fs::path& game) {
  using namespace gow3::setup;
  std::vector<std::string> names;
  std::error_code ec;
  for (fs::directory_iterator it(game / "DLC", ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    const auto h = ReadStfsHeader(it->path());
    if (!h || h->title_id != kGow3TitleId || h->content_type != kDlcType) continue;
    names.push_back(h->display_name.empty() ? Utf8(it->path().filename()) : h->display_name);
  }
  std::sort(names.begin(), names.end());
  return names;
}

bool Tu6Present(const fs::path& game) {
  std::error_code ec;
  return !game.empty() && fs::is_regular_file(game / "TU6" / "default.xexp", ec);
}

std::string RendererText(const fs::path& toml) {
  const gow3::setup::SettingsFile f = gow3::setup::LoadSettings(toml);
  auto value = [&](const char* key, const char* def) {
    const auto it = f.values.find(key);
    return it == f.values.end() ? std::string(def) : it->second;
  };
  std::string renderer = value("gow3_renderer", "native");
  if (renderer == "native") renderer = "Native";
  else if (renderer == "rtv") renderer = "RTV";
  else if (renderer == "rov") renderer = "ROV";
  return renderer + kDot + value("resolution_scale", "1") + "x";
}

// The tail of `text` that fits in max_width, with a leading ellipsis when cut.
std::string FitLeft(const std::string& text, float max_width) {
  if (ImGui::CalcTextSize(text.c_str()).x <= max_width) return text;
  const std::string ellipsis = "\xE2\x80\xA6";
  for (size_t i = 1; i < text.size(); ++i) {
    if ((static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) continue;  // inside a UTF-8 sequence
    const std::string cut = ellipsis + text.substr(i);
    if (ImGui::CalcTextSize(cut.c_str()).x <= max_width) return cut;
  }
  return ellipsis;
}

// A label on the left, a muted value (or a pill) on the right, a line below. With `button`, the
// value sits left of a ghost button at the right edge; returns true when it is pressed.
bool Row(const char* label, const std::string& value, bool pill = false, const char* button = nullptr,
         bool button_enabled = true) {
  using namespace theme;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float w = ImGui::GetContentRegionAvail().x, h = Dp(42.0f), fs = ImGui::GetFontSize();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddText(ImVec2(p.x, p.y + (h - fs) * 0.5f), ImGui::GetColorU32(kText), label);
  float right = p.x + w;
  bool pressed = false;
  if (button) {
    const float bw = ImGui::CalcTextSize(button).x + Dp(24.0f), bh = Dp(28.0f);
    ImGui::SetCursorScreenPos(ImVec2(right - bw, p.y + (h - bh) * 0.5f));
    ImGui::BeginDisabled(!button_enabled);
    pressed = GhostButton(button, ImVec2(bw, bh));
    ImGui::EndDisabled();
    right -= bw + Dp(12.0f);
  }
  if (pill) {
    ImGui::PushFont(nullptr, 13.0f);
    const ImVec2 size(ImGui::CalcTextSize(value.c_str()).x + Dp(20.0f), ImGui::GetFontSize() + Dp(6.0f));
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(right - size.x, p.y + (h - size.y) * 0.5f));
    Pill(value.c_str());
  } else {
    const float label_w = ImGui::CalcTextSize(label).x + Dp(24.0f);
    const std::string shown = FitLeft(value, right - p.x - label_w);
    const float vw = ImGui::CalcTextSize(shown.c_str()).x;
    dl->AddText(ImVec2(right - vw, p.y + (h - fs) * 0.5f), ImGui::GetColorU32(kMuted), shown.c_str());
  }
  dl->AddLine(ImVec2(p.x, p.y + h - Dp(1.0f)), ImVec2(p.x + w, p.y + h - Dp(1.0f)), ImGui::GetColorU32(kLine),
              Dp(1.0f));
  ImGui::SetCursorScreenPos(p);
  ImGui::Dummy(ImVec2(w, h));
  return pressed;
}

// A panel box (the mockup's .box): call EndBox() after its contents.
void BeginBox(const char* id, float width = 0.0f) {
  using namespace theme;
  ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanel);
  ImGui::PushStyleColor(ImGuiCol_Border, Rgb(0x23262B));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Dp(8.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, Dp(1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Dp(12.0f), Dp(10.0f)));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Dp(8.0f), Dp(4.0f)));
  ImGui::BeginChild(id, ImVec2(width, 0),
                    ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_NoScrollbar);
}
void EndBox() {
  ImGui::EndChild();
  ImGui::PopStyleVar(4);
  ImGui::PopStyleColor(2);
}

// Buttons along the bottom edge of the page, right-aligned, in the order given. Returns the label
// of the pressed one, or "". A primary button is drawn in the accent colour with Teko.
struct BottomButton {
  const char* label;
  bool primary = false;
  bool enabled = true;
};
std::string_view BottomButtons(std::initializer_list<BottomButton> buttons) {
  using namespace theme;
  const float h = Dp(40.0f), gap = Dp(8.0f);
  std::vector<float> widths;
  float total = 0.0f;
  for (const BottomButton& b : buttons) {
    float w = ImGui::CalcTextSize(b.label).x + Dp(28.0f);
    if (b.primary) w = std::max(w, Dp(130.0f));
    widths.push_back(w);
    total += w + (widths.size() > 1 ? gap : 0.0f);
  }
  const float bottom = ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y;
  ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - total, bottom - h));
  std::string_view pressed;
  size_t i = 0;
  for (const BottomButton& b : buttons) {
    if (i) ImGui::SameLine(0, gap);
    ImGui::BeginDisabled(!b.enabled);
    if (b.primary) {
      ImGui::PushFont(HeadingFont(), 24.0f);
      if (PrimaryButton(b.label, ImVec2(widths[i], h))) pressed = b.label;
      ImGui::PopFont();
    } else if (GhostButton(b.label, ImVec2(widths[i], h))) {
      pressed = b.label;
    }
    ImGui::EndDisabled();
    ++i;
  }
  return pressed;
}

// ---------------------------------------------------------------------------------------------
// Game files state

struct PlayInfo {
  std::string content;   // "Title Update 6 · RAAM's Shadow · ..."
  std::string renderer;  // "Native · 2x"
};
PlayInfo g_play;

enum class View { kOverview, kSetup, kWorking, kOffer, kRepair };
enum class JobKind { kExtract, kInstall, kRepair, kSwitch };

struct OfferItem {
  FoundPackage pkg;
  bool ticked;
  bool usable;  // false: a TU for another disc version (greyed out)
};

// What the worker did, read by the UI thread only after Job::Finished (the worker is joined then).
struct Outcome {
  std::vector<std::string> installed;  // ids, in install order
  bool tu_installed = false;
  std::optional<SwapResult> swap;
};

struct GameFiles {
  View view = View::kOverview;
  std::string message;  // shown in accent on the current view; cleared when the view changes
  // Set up.
  std::optional<FoundIso> iso;
  std::optional<gow3::setup::DiscImage> disc;
  fs::path target;
  bool target_in_iso = false;
  std::optional<uint64_t> free_bytes;
  std::string drive;  // "D:"
  // New content.
  std::vector<OfferItem> items;
  std::string offer_folder;  // common folder of the offered packages, or empty
  // Running job.
  JobKind kind = JobKind::kExtract;
  Build switch_to = Build::kBase;
  std::vector<std::string> offered;  // ids shown on the offer page when INSTALL was pressed
  std::shared_ptr<Outcome> outcome;
  std::deque<std::pair<double, uint64_t>> samples;  // (seconds, bytes done), last 10 s
};
GameFiles g;
Job g_job;

void Show(View v, std::string message = {}) {
  g.view = v;
  g.message = std::move(message);
}

double Now() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void SaveState(App& app) {
  if (!gow3::setup::SaveState(app.dir / "launcher.ini", app.state)) {
    g.message = "Could not write launcher.ini next to the launcher.";
  }
}

bool SetGame(App& app, const fs::path& folder) {
  if (!gow3::setup::WriteGamePathFile(app.dir / "gow3_game_path.txt", folder)) {
    Show(View::kOverview, "Could not write gow3_game_path.txt next to the launcher.");
    return false;
  }
  app.game = folder;
  RefreshPlayPage(app);
  return true;
}

// The same folders gow3.exe searches (PackageRoots).
std::vector<FoundPackage> ScanPackages(const App& app) {
  using namespace gow3::setup;
  const auto xex = ReadXexInfoFile(app.game / "default.xex");
  return FindPackages(PackageRoots(app.dir, app.game), xex ? xex->media_id : 0);
}

// Fills the offer list (TU first). Returns true when there is something new to offer.
bool BuildOffer(const App& app, bool ignore_declined) {
  const std::vector<FoundPackage> found = ScanPackages(app);
  gow3::setup::LauncherState s = app.state;
  if (ignore_declined) s.declined.clear();
  g.items.clear();
  std::set<std::string> seen;
  for (const FoundPackage* p : gow3::setup::NewPackages(found, s, app.state.build)) {
    if (seen.insert(p->id).second) g.items.push_back({*p, true, true});
  }
  const bool any = !g.items.empty();
  if (any && app.state.build == Build::kBase) {
    for (const FoundPackage& p : found) {
      if (p.is_tu && !p.matches_disc && seen.insert(p.id).second) g.items.push_back({p, false, false});
    }
  }
  std::stable_partition(g.items.begin(), g.items.end(), [](const OfferItem& i) { return i.pkg.is_tu; });
  std::stable_partition(g.items.begin(), g.items.end(), [](const OfferItem& i) { return i.usable; });
  g.offer_folder.clear();
  fs::path common;
  for (const OfferItem& i : g.items) {
    const fs::path dir = i.pkg.file.parent_path();
    if (common.empty()) common = dir;
    else if (common != dir) return any;
  }
  g.offer_folder = U8(common);
  return any;
}

void UpdateTarget();

// Valid ISO -> the Set up page for it. Returns false (and says why) otherwise.
bool UseIso(const fs::path& file) {
  using namespace gow3::setup;
  std::string error;
  auto disc = DiscImage::Open(file, &error);
  std::optional<XexInfo> xex;
  if (disc) {
    if (auto bytes = disc->ReadFile("default.xex", 0x10000)) xex = ReadXexInfo(*bytes);
  }
  if (!xex || xex->title_id != kGow3TitleId) {
    g.message = "That file isn't a Gears of War 3 disc image.";
    return false;
  }
  std::error_code ec;
  const uint64_t size = fs::file_size(file, ec);
  g.iso = FoundIso{file, ec ? 0 : size, *xex};
  g.disc = std::move(disc);
  g.target = file.parent_path() / L"Gears of War 3";
  UpdateTarget();
  Show(View::kSetup);
  return true;
}

std::wstring Lower(std::wstring s) {
  std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  return s;
}

// Free space on the target's drive and whether the target lies inside the ISO's own file path.
void UpdateTarget() {
  std::error_code ec;
  fs::path t = fs::weakly_canonical(g.target, ec);
  if (ec) t = g.target;
  fs::path iso = fs::weakly_canonical(g.iso->file, ec);
  if (ec) iso = g.iso->file;
  const std::wstring ts = Lower(t.wstring()), is = Lower(iso.wstring());
  g.target_in_iso = ts == is || (ts.size() > is.size() && ts.compare(0, is.size(), is) == 0 &&
                                 (ts[is.size()] == L'\\' || ts[is.size()] == L'/'));
  g.free_bytes.reset();
  const fs::path existing = ExistingAncestor(g.target);
  g.drive = U8(g.target.root_name());
  ULARGE_INTEGER avail{};
  if (!existing.empty() && GetDiskFreeSpaceExW(existing.c_str(), &avail, nullptr, nullptr)) {
    g.free_bytes = avail.QuadPart;
  }
}

// After a game folder is set (found, picked or extracted): the package step, then Play.
void AfterGameSet(App& app) {
  if (BuildOffer(app, false)) {
    Show(View::kOffer);
    app.page = App::Page::kGameFiles;
  } else {
    Show(View::kOverview);
    app.page = App::Page::kPlay;
  }
}

// Picked from "Pick another file" / "Change…": an ISO, a game folder or nothing.
void HandlePicked(App& app, const fs::path& picked) {
  if (picked.empty()) return;
  std::error_code ec;
  if (fs::is_directory(picked, ec)) {
    if (!gow3::setup::IsGameFolder(picked)) {
      g.message = "That folder isn't a Gears of War 3 game folder (it needs default.xex and GearGame).";
      return;
    }
    if (SetGame(app, picked)) AfterGameSet(app);
    return;
  }
  UseIso(picked);
}

// Polls every 250 ms for up to 30 s. kFailed: still running.
ExtractResult WaitForGameExit(const fs::path& exe, const Progress& progress) {
  for (int i = 0; gow3::setup::IsGameRunning(exe); ++i) {
    if (i >= 120) return ExtractResult::kFailed;
    if (!progress(0, 0, "Waiting for Gears of War 3 to close")) return ExtractResult::kCancelled;
    Sleep(250);
  }
  return ExtractResult::kOk;
}

void StartJob(JobKind kind, Job::Work work) {
  g.kind = kind;
  g.samples.clear();
  Show(View::kWorking);
  g_job.Start(std::move(work));
}

void StartExtract() {
  const gow3::setup::DiscImage disc = *g.disc;
  const fs::path target = g.target;
  StartJob(JobKind::kExtract, [disc, target](const Progress& progress, std::string* error) {
    std::error_code ec;
    fs::create_directories(target, ec);
    if (ec) return gow3::setup::Fail(error, "Could not create " + U8(target) + ".");
    return disc.ExtractAll(target, progress, error);
  });
}

std::string SwapFailure(SwapResult r, Build to) {
  if (r == SwapResult::kGameRunning) return kCloseFirst;
  return std::string("Could not switch gow3.exe to the ") + (to == Build::kTu6 ? "Title Update 6" : "base") +
         " build (is builds\\gow3-" + (to == Build::kTu6 ? "tu6" : "base") + ".exe there?).";
}

// Waits for gow3.exe to close, then swaps the build. kFailed (with the message) when it can't.
ExtractResult WaitAndSwap(const fs::path& dir, Build to, const Progress& progress, std::string* error) {
  const ExtractResult w = WaitForGameExit(dir / "gow3.exe", progress);
  if (w == ExtractResult::kFailed) return gow3::setup::Fail(error, kCloseFirst);
  if (w != ExtractResult::kOk) return w;
  const SwapResult s = gow3::setup::SwapBuild(dir, to, std::chrono::seconds(0));
  return s == SwapResult::kOk ? ExtractResult::kOk : gow3::setup::Fail(error, SwapFailure(s, to));
}

// TU (through InstallTitleUpdate), then the DLC copies, then (offer page, base build) the TU6 build
// if the TU went in. kRepair reinstalls the TU only. Any failure, the swap included, fails the job
// and nothing is recorded (installing again just overwrites <game>\TU6 and the DLC copies).
void StartInstall(const App& app, const std::vector<FoundPackage>& picked, JobKind kind) {
  auto outcome = std::make_shared<Outcome>();
  g.outcome = outcome;
  const fs::path dir = app.dir, game = app.game;
  const bool swap = kind == JobKind::kInstall && app.state.build == Build::kBase;
  StartJob(kind, [outcome, picked, swap, dir, game](const Progress& progress, std::string* error) {
    using namespace gow3::setup;
    uint64_t total = 0, base = 0;
    for (const FoundPackage& p : picked) total += p.size;
    const bool has_tu = std::any_of(picked.begin(), picked.end(), [](auto& p) { return p.is_tu; });
    if (has_tu && swap) {
      const ExtractResult w = WaitForGameExit(dir / "gow3.exe", progress);
      if (w == ExtractResult::kFailed) return Fail(error, kCloseFirst);
      if (w != ExtractResult::kOk) return w;
    }
    for (const FoundPackage& p : picked) {
      if (!p.is_tu) continue;
      const Progress scaled = [&](uint64_t done, uint64_t of, const std::string& current) {
        return progress(base + (of ? static_cast<uint64_t>(double(done) / of * p.size) : 0), total,
                        "TU6\\" + current);
      };
      const ExtractResult r = InstallTitleUpdate(p, game, scaled, error);
      if (r != ExtractResult::kOk) return r;
      base += p.size;
      outcome->installed.push_back(p.id);
      outcome->tu_installed = true;
    }
    for (const FoundPackage& p : picked) {
      if (p.is_tu) continue;
      if (!progress(base, total, "DLC\\" + Utf8(p.file.filename()))) return ExtractResult::kCancelled;
      std::error_code ec;
      if (!fs::equivalent(p.file, game / "DLC" / p.file.filename(), ec) && !InstallDlc(p, game, error)) {
        return ExtractResult::kFailed;
      }
      base += p.size;
      outcome->installed.push_back(p.id);
    }
    progress(total, total, "");
    if (swap && outcome->tu_installed) {
      // The game may have been started during the install: wait for it again right before the swap.
      const ExtractResult s = WaitAndSwap(dir, Build::kTu6, progress, error);
      if (s != ExtractResult::kOk) return s;
      outcome->swap = SwapResult::kOk;
    }
    return ExtractResult::kOk;
  });
}

void StartSwitch(const App& app, Build to) {
  g.outcome = std::make_shared<Outcome>();
  g.switch_to = to;
  const fs::path dir = app.dir;
  StartJob(JobKind::kSwitch, [dir, to](const Progress& progress, std::string* error) {
    return WaitAndSwap(dir, to, progress, error);
  });
}

// After a flow that gow3.exe handed over: start the game (Ruling 13); otherwise stay here.
void FlowDone(App& app, std::string message = {}) {
  Show(View::kOverview, std::move(message));
  if (app.start != App::Start::kNormal) {
    app.StartGameAndQuit();
    if (!app.error.empty()) app.page = App::Page::kPlay;
  }
}

// ---------------------------------------------------------------------------------------------
// Views

void DrawSetup(App& app) {
  using namespace theme;
  PageTitle("SET UP YOUR GAME");
  if (!g.iso) {
    ImGui::TextColored(kMuted, "Pick your ISO or game folder");
    ImGui::Dummy(ImVec2(0, Dp(4.0f)));
    BeginBox("##none");
    ImGui::TextWrapped("No Gears of War 3 ISO or game folder was found next to the launcher.");
    ImGui::TextColored(kMuted, "A game folder holds default.xex and GearGame.");
    EndBox();
    if (!g.message.empty()) ImGui::TextColored(kAccent, "%s", g.message.c_str());
    const std::string_view b =
        app.game.empty() ? BottomButtons({{"PICK", true}}) : BottomButtons({{"Back"}, {"PICK", true}});
    if (b == "Back") Show(View::kOverview);
    if (b == "PICK") {
      g.message.clear();
      HandlePicked(app, PickIsoOrFolder(app.hwnd));
    }
    return;
  }

  ImGui::TextColored(kMuted, "Found your copy of Gears of War 3:");
  BeginBox("##iso");
  ImGui::TextUnformatted(U8(g.iso->file.filename()).c_str());
  const std::string where = kDot + Gb(g.iso->size) + " GB";
  const float avail = ImGui::GetContentRegionAvail().x;
  const std::string folder = FitLeft(U8(g.iso->file.parent_path()), avail - ImGui::CalcTextSize(where.c_str()).x);
  ImGui::TextColored(kMuted, "%s%s", folder.c_str(), where.c_str());
  ImGui::TextColored(kMuted, "%s", Ids(g.iso->xex.title_id, g.iso->xex.media_id).c_str());
  EndBox();

  ImGui::Dummy(ImVec2(0, Dp(2.0f)));
  ImGui::TextColored(kMuted, "Extract it to");
  const float browse_w = ImGui::CalcTextSize("Browse").x + Dp(28.0f), gap = Dp(6.0f);
  BeginBox("##target", ImGui::GetContentRegionAvail().x - browse_w - gap);
  ImGui::TextUnformatted(FitLeft(U8(g.target), ImGui::GetContentRegionAvail().x).c_str());
  EndBox();
  const float box_h = ImGui::GetItemRectSize().y;
  ImGui::SameLine(0, gap);
  if (GhostButton("Browse", ImVec2(browse_w, box_h))) {
    if (fs::path picked = PickFolder(app.hwnd, g.target); !picked.empty()) {
      std::error_code ec;
      // An empty folder, one named "Gears of War 3" or a game folder is used as it is; any other gets
      // a "Gears of War 3" folder inside.
      const bool as_is = fs::is_empty(picked, ec) || Lower(picked.filename().wstring()) == L"gears of war 3" ||
                         gow3::setup::IsGameFolder(picked);
      g.target = as_is ? picked : picked / L"Gears of War 3";
      UpdateTarget();
    }
  }

  const uint64_t needed = g.disc->TotalBytes();
  const bool enough = g.free_bytes && *g.free_bytes >= needed;
  std::string space = "Needs " + Gb(needed) + " GB free";
  if (g.free_bytes) space += " (" + g.drive + " has " + Gb(*g.free_bytes) + " GB)";
  space += ". Your ISO is not changed.";
  ImGui::PushFont(nullptr, 14.0f);
  ImGui::TextColored(enough ? kMuted : kAccent, "%s", space.c_str());
  if (g.target_in_iso) ImGui::TextColored(kAccent, "Pick a folder outside the ISO file.");
  ImGui::PopFont();
  if (!g.message.empty()) ImGui::TextColored(kAccent, "%s", g.message.c_str());

  const bool can_extract = enough && !g.target_in_iso;
  const std::string_view b =
      app.game.empty() ? BottomButtons({{"Pick another file"}, {"EXTRACT", true, can_extract}})
                       : BottomButtons({{"Back"}, {"Pick another file"}, {"EXTRACT", true, can_extract}});
  if (b == "Back") Show(View::kOverview);
  if (b == "Pick another file") {
    g.message.clear();
    HandlePicked(app, PickIsoOrFolder(app.hwnd));
  }
  if (b == "EXTRACT") StartExtract();
}

void DrawWorking() {
  using namespace theme;
  static const char* const kTitles[] = {"EXTRACTING", "INSTALLING", "INSTALLING", "SWITCHING BUILD"};
  PageTitle(kTitles[static_cast<int>(g.kind)]);
  const uint64_t done = g_job.Done(), total = g_job.Total();
  ImGui::TextColored(kMuted, "%s", FitLeft(g_job.Current(), ImGui::GetContentRegionAvail().x).c_str());

  // Bar.
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float w = ImGui::GetContentRegionAvail().x, h = Dp(6.0f);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(kNavActive), h * 0.5f);
  if (total) {
    const float f = std::min(1.0f, static_cast<float>(double(done) / total));
    if (f > 0.0f) dl->AddRectFilled(p, ImVec2(p.x + w * f, p.y + h), ImGui::GetColorU32(kAccent), h * 0.5f);
  }
  ImGui::Dummy(ImVec2(w, h));

  if (total) {
    ImGui::TextColored(kMuted, "%s of %s GB", Gb(done).c_str(), Gb(total).c_str());
    // Rate over the last 10 s (PollGameFiles takes the samples).
    if (g.samples.size() >= 2 && g.samples.back().first - g.samples.front().first >= 2.0 &&
        g.samples.back().second > g.samples.front().second && done < total) {
      const auto& [t0, d0] = g.samples.front();
      const auto& [t1, d1] = g.samples.back();
      const double left = (total - done) / ((d1 - d0) / (t1 - t0));
      const std::string eta = "about " + std::to_string(std::max(1, static_cast<int>(std::ceil(left / 60.0)))) +
                              " min left";
      ImGui::SameLine(ImGui::GetCursorStartPos().x + w - ImGui::CalcTextSize(eta.c_str()).x);
      ImGui::TextColored(kMuted, "%s", eta.c_str());
    }
  }

  if (g.kind == JobKind::kExtract) {
    const float box_y = ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - Dp(40.0f) - Dp(70.0f);
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY() + Dp(8.0f), box_y));
    BeginBox("##wait");
    ImGui::PushFont(nullptr, 14.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped(
        "While you wait: pick your settings in the tabs on the left. They are saved for the first launch.");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    EndBox();
  }
  const bool cancelling = g_job.Cancelling();
  if (BottomButtons({{cancelling ? "Cancelling\xE2\x80\xA6" : "Cancel", false, !cancelling}}) == "Cancel") {
    g_job.Cancel();
  }
}

void DrawOffer(App& app) {
  using namespace theme;
  PageTitle("NEW CONTENT FOUND");
  if (g.offer_folder.empty()) {
    ImGui::TextColored(kMuted, "Next to your game:");
  } else {
    const std::string lead = "Next to your game, in ";
    const std::string folder =
        FitLeft(g.offer_folder, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize((lead + ":").c_str()).x);
    ImGui::TextColored(kMuted, "%s%s:", lead.c_str(), folder.c_str());
  }
  BeginBox("##items");
  int index = 0;
  for (OfferItem& item : g.items) {
    ImGui::PushID(index++);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, line = ImGui::GetTextLineHeight();
    const float row_h = line * 2 + Dp(8.0f);
    ImGui::BeginDisabled(!item.usable);
    if (ImGui::InvisibleButton("##tick", ImVec2(w, row_h))) item.ticked = !item.ticked;
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float box = Dp(13.0f);
    const ImVec2 b0(p.x, p.y + Dp(2.0f) + (line - box) * 0.5f), b1(b0.x + box, b0.y + box);
    if (item.ticked) {
      dl->AddRectFilled(b0, b1, ImGui::GetColorU32(kAccent), Dp(3.0f));
      const ImU32 white = ImGui::GetColorU32(ImVec4(1, 1, 1, 1));
      dl->AddLine(ImVec2(b0.x + box * 0.22f, b0.y + box * 0.52f), ImVec2(b0.x + box * 0.42f, b0.y + box * 0.72f),
                  white, Dp(1.6f));
      dl->AddLine(ImVec2(b0.x + box * 0.42f, b0.y + box * 0.72f), ImVec2(b0.x + box * 0.78f, b0.y + box * 0.3f),
                  white, Dp(1.6f));
    } else {
      dl->AddRect(b0, b1, ImGui::GetColorU32(hovered ? kMuted : kOutline), Dp(3.0f), 0, Dp(1.0f));
    }
    const float x = b1.x + Dp(9.0f);
    std::string name, extra;
    if (item.pkg.is_tu) {
      name = item.usable ? "Title Update 6" : "Title Update";
      extra = item.usable ? " (needed for the DLC)" : " (for another disc version)";
    } else {
      name = item.pkg.header.display_name.empty() ? U8(item.pkg.file.filename()) : item.pkg.header.display_name;
    }
    const ImVec2 t(x, p.y + Dp(2.0f));
    dl->AddText(t, ImGui::GetColorU32(item.usable ? kText : kMuted), name.c_str());
    if (!extra.empty()) {
      dl->AddText(ImVec2(t.x + ImGui::CalcTextSize(name.c_str()).x, t.y), ImGui::GetColorU32(kMuted), extra.c_str());
    }
    ImGui::PushFont(nullptr, 13.0f);
    std::string ids = Ids(item.pkg.header.title_id, item.pkg.header.media_id);
    if (g.offer_folder.empty()) {
      ids += kDot;
      ids += FitLeft(U8(item.pkg.file.parent_path()),
                     p.x + w - x - ImGui::CalcTextSize((ids + "  ").c_str()).x);
    }
    dl->AddText(ImVec2(x, t.y + line + Dp(1.0f)), ImGui::GetColorU32(kMuted), ids.c_str());
    ImGui::PopFont();
    ImGui::EndDisabled();
    ImGui::PopID();
  }
  EndBox();
  ImGui::PushFont(nullptr, 14.0f);
  ImGui::TextColored(kMuted, "%s", kNoteSaves);
  ImGui::PopFont();
  if (!g.message.empty()) ImGui::TextColored(kAccent, "%s", g.message.c_str());

  const bool any = std::any_of(g.items.begin(), g.items.end(), [](auto& i) { return i.usable && i.ticked; });
  const char* keep = app.state.build == Build::kBase ? "Keep base game" : "Not now";
  const std::string_view b = BottomButtons({{keep}, {"INSTALL", true, any}});
  if (b == keep) {
    for (const OfferItem& i : g.items) {
      if (i.usable) app.state.declined.insert(i.pkg.id);
    }
    SaveState(app);
    FlowDone(app, g.message);
  } else if (b == "INSTALL") {
    std::vector<FoundPackage> picked;
    g.offered.clear();
    for (const OfferItem& i : g.items) {
      if (!i.usable) continue;
      g.offered.push_back(i.pkg.id);
      if (i.ticked) picked.push_back(i.pkg);
    }
    StartInstall(app, picked, JobKind::kInstall);
  }
}

void DrawRepair(App& app) {
  using namespace theme;
  PageTitle("TITLE UPDATE 6 MISSING");
  const std::string where = U8(app.game / "TU6");
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(kMuted, "Title Update 6 is missing from %s", where.c_str());
  ImGui::PopTextWrapPos();
  ImGui::Dummy(ImVec2(0, Dp(4.0f)));
  BeginBox("##repair");
  ImGui::TextWrapped("Install it again from your Title Update 6 package, or switch back to the base game.");
  ImGui::PushFont(nullptr, 14.0f);
  ImGui::TextColored(kMuted, "The launcher looks next to the game and next to itself.");
  ImGui::PopFont();
  EndBox();
  if (!g.message.empty()) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kAccent, "%s", g.message.c_str());
    ImGui::PopTextWrapPos();
  }
  const std::string_view b = BottomButtons({{"Switch to base game"}, {"INSTALL IT AGAIN", true}});
  if (b == "Switch to base game") StartSwitch(app, Build::kBase);
  if (b == "INSTALL IT AGAIN") {
    const std::vector<FoundPackage> found = ScanPackages(app);
    const auto tu = std::find_if(found.begin(), found.end(), [](auto& p) { return p.is_tu && p.matches_disc; });
    if (tu == found.end()) {
      g.message = "No Title Update 6 package for this disc was found next to the game or the launcher.";
    } else {
      StartInstall(app, {*tu}, JobKind::kRepair);
    }
  }
}

void DrawOverview(App& app) {
  using namespace theme;
  PageTitle("GAME FILES");
  ImGui::Dummy(ImVec2(0, Dp(4.0f)));
  if (Row("Game folder", app.game.empty() ? std::string("Not set") : U8(app.game), false, "Change\xE2\x80\xA6")) {
    g.message.clear();
    HandlePicked(app, PickIsoOrFolder(app.hwnd));
    if (g.view != View::kOverview) return;
  }
  const bool tu6 = app.state.build == Build::kTu6;
  const bool can_switch = !app.game.empty() && (tu6 || Tu6Present(app.game));
  if (Row("Build", tu6 ? "Title Update 6" : "Base", false, "Switch", can_switch)) {
    StartSwitch(app, tu6 ? Build::kBase : Build::kTu6);
    return;
  }
  std::string content;
  if (Tu6Present(app.game)) content = "Title Update 6";
  if (!app.game.empty()) {
    for (const std::string& dlc : InstalledDlc(app.game)) content += (content.empty() ? "" : kDot) + dlc;
  }
  Row("Installed content", content.empty() ? std::string("None") : content);
  {
    // Check for updates: the switch (launcher.ini, once a day at start) left of Check now.
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, h = Dp(42.0f);
    const char* label = UpdateCheckRunning() ? "Checking\xE2\x80\xA6" : "Check now";
    if (Row("Check for updates", "", false, label, UpdateChecksAvailable() && !UpdateCheckRunning())) {
      CheckForUpdateNow(app);
    }
    const float bw = ImGui::CalcTextSize(label).x + Dp(24.0f);
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - bw - Dp(12.0f) - Dp(36.0f), p.y + (h - Dp(20.0f)) * 0.5f));
    if (Toggle("##check_updates", &app.state.check_updates)) SaveState(app);
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    ImGui::Dummy(ImVec2(0, 0));
    const std::string& status = UpdateStatus();
    if (!status.empty()) {
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextColored(kMuted, "%s", status.c_str());
      ImGui::PopTextWrapPos();
    }
  }
  if (!g.message.empty()) {
    ImGui::Dummy(ImVec2(0, Dp(2.0f)));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kAccent, "%s", g.message.c_str());
    ImGui::PopTextWrapPos();
  }
  ImGui::BeginDisabled(app.game.empty());
  const bool look = !BottomButtons({{"Look for TU6 / DLC"}}).empty();
  ImGui::EndDisabled();
  if (look) {
    if (BuildOffer(app, true)) Show(View::kOffer);
    else g.message = "No new Title Update 6 or DLC packages were found next to the game or the launcher.";
  }
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Play page

void RefreshPlayPage(const App& app) {
  g_play = {};
  if (app.state.build == Build::kTu6) {
    g_play.content = "Title Update 6";
    if (!app.game.empty()) {
      for (const std::string& dlc : InstalledDlc(app.game)) g_play.content += kDot + dlc;
    }
  } else {
    g_play.content = "Base game";
  }
  g_play.renderer = RendererText(app.dir / "gow3.toml");
}

void DrawPlayPage(App& app) {
  using namespace theme;
  const bool busy = g_job.Running();
  const bool ready = app.CanPlay() && !busy;
  PageTitle(ready ? "READY" : "SETUP NEEDED", ready ? kTitle : kAccent);
  std::string line = g_play.content;
  if (busy) line = "Wait for the Game files page to finish.";
  else if (app.game.empty()) line = "Choose your game folder on the Game files page.";
  else if (!ready) line = "gow3.exe is missing from the launcher's folder.";
  ImGui::TextColored(kMuted, "%s", line.c_str());
  ImGui::Dummy(ImVec2(0, Dp(10.0f)));

  Row("Game folder", app.game.empty() ? std::string("Not set") : U8(app.game));
  Row("Build", app.state.build == Build::kTu6 ? "TU6" : "BASE", true);
  Row("Renderer", g_play.renderer);

  const float button_h = Dp(54.0f);
  const float bottom = ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y;
  if (!app.error.empty()) {
    ImGui::SetCursorPosY(bottom - button_h - ImGui::GetTextLineHeightWithSpacing() - Dp(4.0f));
    ImGui::TextColored(kAccent, "%s", app.error.c_str());
  }
  ImGui::SetCursorPosY(bottom - button_h);
  ImGui::BeginDisabled(!ready);
  ImGui::PushFont(HeadingFont(), 30.0f);
  if (PrimaryButton("PLAY", ImVec2(0, button_h))) app.StartGameAndQuit();
  ImGui::PopFont();
  ImGui::EndDisabled();
}

// ---------------------------------------------------------------------------------------------
// Game files page

void InitGameFiles(App& app) {
  using namespace gow3::setup;
  if (app.game.empty()) {
    const FindResult found = FindGame(SearchRoots(app.dir, 2));
    if (found.game) {
      if (SetGame(app, *found.game)) AfterGameSet(app);
    } else if (found.isos.empty() || !UseIso(found.isos.front().file)) {
      Show(View::kSetup);
    }
    return;
  }
  if (app.start == App::Start::kRepair) {
    Show(View::kRepair);
  } else if (app.start == App::Start::kOffer && BuildOffer(app, false)) {
    Show(View::kOffer);
  } else if (app.start == App::Start::kOffer) {
    // Handed over with nothing new for us (Ruling 19): don't strand the player here.
    FlowDone(app);
  } else {
    Show(View::kOverview);
  }
}

void PollGameFiles(App& app) {
  if (g_job.Running()) {  // ETA samples: (seconds, bytes done) over the last 10 s
    const double now = Now();
    if (g.samples.empty() || now - g.samples.back().first >= 0.25) g.samples.emplace_back(now, g_job.Done());
    while (g.samples.size() > 2 && now - g.samples.front().first > 10.0) g.samples.pop_front();
  }
  std::string error;
  const std::optional<ExtractResult> r = g_job.Finished(&error);
  if (!r) return;
  const std::shared_ptr<Outcome> outcome = std::move(g.outcome);
  if (g.kind == JobKind::kExtract) {
    if (*r == ExtractResult::kOk && gow3::setup::IsGameFolder(g.target)) {
      if (SetGame(app, g.target)) AfterGameSet(app);
      return;
    }
    UpdateTarget();  // free space changed
    if (*r == ExtractResult::kCancelled) {
      Show(View::kSetup, "Extraction cancelled. The files written so far stay; extract again to finish.");
    } else if (*r == ExtractResult::kFailed) {
      Show(View::kSetup, "Extraction failed: " + error);
    } else {
      Show(View::kSetup, "The extracted files aren't a Gears of War 3 game folder.");
    }
    return;
  }
  if (g.kind == JobKind::kSwitch) {
    if (*r == ExtractResult::kOk) {
      app.state.build = g.switch_to;
      SaveState(app);
      RefreshPlayPage(app);
      FlowDone(app, g.message);
    } else {
      const View back = app.start == App::Start::kRepair && app.state.build == Build::kTu6 && !Tu6Present(app.game)
                            ? View::kRepair
                            : View::kOverview;
      Show(back, *r == ExtractResult::kCancelled ? std::string() : error);
    }
    return;
  }
  // Install (offer page) or repair (TU only, no swap). A failure, the swap included, records nothing:
  // the offer stays as it was.
  if (*r != ExtractResult::kOk) {
    Show(g.kind == JobKind::kInstall ? View::kOffer : View::kRepair,
         *r == ExtractResult::kCancelled ? std::string() : error);
    return;
  }
  for (const std::string& id : outcome->installed) {
    app.state.installed.insert(id);
    app.state.declined.erase(id);
  }
  if (g.kind == JobKind::kInstall) {
    for (const std::string& id : g.offered) {
      if (!app.state.installed.count(id)) app.state.declined.insert(id);
    }
  }
  if (outcome->swap == SwapResult::kOk) app.state.build = Build::kTu6;
  SaveState(app);
  RefreshPlayPage(app);
  FlowDone(app, g.message);
  // From a manual "Look for TU6 / DLC" (Ruling 18): back to Play with the new state.
  if (g.kind == JobKind::kInstall && app.start == App::Start::kNormal) app.page = App::Page::kPlay;
}

void DrawGameFilesPage(App& app) {
  switch (g.view) {
    case View::kSetup: DrawSetup(app); break;
    case View::kWorking: DrawWorking(); break;
    case View::kOffer: DrawOffer(app); break;
    case View::kRepair: DrawRepair(app); break;
    case View::kOverview: DrawOverview(app); break;
  }
}

bool GameFilesBusy() { return g_job.Running(); }

void ShutdownGameFiles() { g_job.Stop(); }
