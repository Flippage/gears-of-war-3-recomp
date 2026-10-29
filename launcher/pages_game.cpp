#include "pages_game.h"

#include <shobjidl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <deque>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "app.h"
#include "pages_settings.h"
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
using gow3::setup::Utf8;

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

fs::path PickPackageOrFolder(HWND owner) {
  ComPtr<IFileOpenDialog> dialog;
  if (FAILED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
    return {};
  }
  DWORD options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
  dialog->SetTitle(L"Pick your Title Update 6 or DLC package, or a folder that holds them");
  const COMDLG_FILTERSPEC filters[] = {{L"All files", L"*.*"}};
  dialog->SetFileTypes(1, filters);
  PickerEvents events;
  ComPtr<IFileDialogCustomize> customize;
  if (SUCCEEDED(dialog->QueryInterface(IID_PPV_ARGS(&customize)))) {
    customize->AddPushButton(kUseFolderButton, L"Search this folder");
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

// "7.6 GB", or "3.2 MB" below 1 GB.
std::string Size(uint64_t bytes) {
  char buf[32];
  if (bytes >= (1ull << 30)) std::snprintf(buf, sizeof(buf), "%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
  else std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
  return buf;
}

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

// gow3_tu6_folder from gow3.toml (empty when unset), read by RefreshPlayPage.
fs::path g_tu6_configured;

// Where gow3.exe will look for Title Update 6 (gow3::setup::TitleUpdateCandidates).
std::vector<fs::path> Tu6Candidates(const App& app) {
  return gow3::setup::TitleUpdateCandidates(app.game, app.dir, g_tu6_configured);
}

// The TU6 build would find its title update.
bool Tu6Present(const App& app) {
  return !app.game.empty() && gow3::setup::FindTitleUpdateFolderIn(app.game, app.dir, g_tu6_configured).has_value();
}

std::string RendererText(const gow3::setup::SettingsFile& f) {
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
    ImGui::PushFont(nullptr, theme::kSmallSize);
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

enum class View { kOverview, kSetup, kWorking, kAskContent, kOffer, kRepair, kRestoreBuild };
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
  std::string install_names;  // "Title Update 6 · RAAM's Shadow": what INSTALL was pressed for
  uint32_t media_id = 0;      // the disc the offered TUs must match (the game's, or the ISO's on Set up)
  std::vector<FoundPackage> pending;  // ticked on Set up: installed once the extraction is done
  bool ask_existing = false;  // the ask page is the one-time "now supported" question
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
  SettingsGameChanged(app);  // the Game Language choices
  return true;
}

// The same folders gow3.exe searches (PackageRoots).
std::vector<FoundPackage> ScanPackages(const App& app) {
  using namespace gow3::setup;
  const auto xex = ReadXexInfoFile(app.game / "default.xex");
  return FindPackages(PackageRoots(app.dir, app.game), xex ? xex->media_id : 0);
}

// Usable TUs first, then the greyed ones (another disc version), then the DLC.
void SortItems() {
  std::stable_partition(g.items.begin(), g.items.end(), [](const OfferItem& i) { return i.usable; });
  std::stable_partition(g.items.begin(), g.items.end(), [](const OfferItem& i) { return i.pkg.is_tu; });
}

std::string ItemName(const OfferItem& i) {
  if (i.pkg.is_tu) return "Title Update 6";
  return i.pkg.header.display_name.empty() ? Utf8(i.pkg.file.filename()) : i.pkg.header.display_name;
}

// Fills the offer list from `found`. Returns true when there is something new to offer.
bool FillOffer(const std::vector<FoundPackage>& found, gow3::setup::LauncherState s, bool ignore_declined) {
  if (ignore_declined) s.declined.clear();
  g.items.clear();
  std::set<std::string> seen;
  for (const FoundPackage* p : gow3::setup::NewPackages(found, s, s.build)) {
    if (seen.insert(p->id).second) g.items.push_back({*p, true, true});
  }
  const bool any = !g.items.empty();
  if (any && s.build == Build::kBase) {
    for (const FoundPackage& p : found) {
      if (p.is_tu && !p.matches_disc && seen.insert(p.id).second) g.items.push_back({p, false, false});
    }
  }
  SortItems();
  return any;
}

bool BuildOffer(const App& app, bool ignore_declined) {
  const auto xex = gow3::setup::ReadXexInfoFile(app.game / "default.xex");
  g.media_id = xex ? xex->media_id : 0;
  return FillOffer(ScanPackages(app), app.state, ignore_declined);
}

// Set up: the packages around the launcher and the ISO, for the ISO's disc, as for a new install.
void BuildSetupOffer(const App& app) {
  using namespace gow3::setup;
  g.media_id = g.iso->xex.media_id;
  LauncherState fresh;
  FillOffer(FindPackages(PackageRoots(app.dir, g.iso->file.parent_path()), g.media_id), fresh, true);
}

// The player picked a package or a folder ("Add files…"): adds what it holds to the offer list.
void AddPicked(const App& app, const fs::path& picked) {
  using namespace gow3::setup;
  if (picked.empty()) return;
  const bool setup = g.view == View::kSetup;  // nothing is installed yet
  const std::vector<FoundPackage> found = FindPackagesAt(picked, g.media_id);
  int added = 0, installed = 0;
  for (const FoundPackage& p : found) {
    if (!setup && (app.state.installed.count(p.id) || (p.is_tu && app.state.build == Build::kTu6))) {
      ++installed;
      continue;
    }
    if (std::any_of(g.items.begin(), g.items.end(), [&](const OfferItem& i) { return i.pkg.id == p.id; })) continue;
    const bool usable = !p.is_tu || p.matches_disc;
    g.items.push_back({p, usable, usable});
    ++added;
  }
  SortItems();
  std::string message;
  if (found.empty()) {
    message = "No Gears of War 3 Title Update or DLC package was found in " + Utf8(picked) + ".";
  } else if (!added) {
    message = installed ? "That is already installed." : "That is already in the list.";
  }
  g.message = std::move(message);  // stays on the page it was picked from
}

void UpdateTarget();

// Valid ISO -> the Set up page for it. Returns false (and says why) otherwise.
bool UseIso(const App& app, const fs::path& file) {
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
  BuildSetupOffer(app);
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
  g.drive = Utf8(g.target.root_name());
  ULARGE_INTEGER avail{};
  if (!existing.empty() && GetDiskFreeSpaceExW(existing.c_str(), &avail, nullptr, nullptr)) {
    g.free_bytes = avail.QuadPart;
  }
}

// After a game folder is set (found, picked or extracted): the TU6 / DLC step (offered when found,
// asked about when not), then Play.
void AfterGameSet(App& app) {
  app.page = App::Page::kGameFiles;
  g.ask_existing = false;
  Show(BuildOffer(app, false) ? View::kOffer : View::kAskContent);
}

// The TU6 / DLC question was answered (or the content was chosen on Set up): never asked again.
void ContentAsked(App& app) {
  if (app.state.content_asked) return;
  app.state.content_asked = true;
  SaveState(app);
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
  UseIso(app, picked);
}

// gow3::setup::WaitForGameExit for up to 30 s, saying so on the progress line. kFailed: still running.
ExtractResult WaitForGame(const fs::path& exe, const Progress& progress) {
  using gow3::setup::WaitResult;
  const WaitResult w = gow3::setup::WaitForGameExit(
      exe, std::chrono::seconds(30), [&] { return !progress(0, 0, "Waiting for Gears of War 3 to close"); });
  return w == WaitResult::kExited ? ExtractResult::kOk
         : w == WaitResult::kCancelled ? ExtractResult::kCancelled
                                       : ExtractResult::kFailed;
}

void StartJob(JobKind kind, Job::Work work) {
  g.kind = kind;
  g.samples.clear();
  Show(View::kWorking);
  g_job.Start(std::move(work));
}

void StartExtract() {
  g.pending.clear();
  g.offered.clear();
  g.install_names.clear();
  for (const OfferItem& i : g.items) {
    if (!i.usable) continue;
    g.offered.push_back(i.pkg.id);
    if (!i.ticked) continue;
    g.pending.push_back(i.pkg);
    g.install_names += (g.install_names.empty() ? "" : kDot) + ItemName(i);
  }
  const gow3::setup::DiscImage disc = *g.disc;
  const fs::path target = g.target;
  StartJob(JobKind::kExtract, [disc, target](const Progress& progress, std::string* error) {
    std::error_code ec;
    fs::create_directories(target, ec);
    if (ec) return gow3::setup::Fail(error, "Could not create " + Utf8(target) + ".");
    // Until the extraction finishes, the folder is not a game folder (IsGameFolder), so an
    // interrupted one (cancel, crash, power cut) is never used as one.
    const fs::path marker = target / gow3::setup::kExtractingMarker;
    {
      std::ofstream out(marker, std::ios::binary | std::ios::trunc);
      if (!out) return gow3::setup::Fail(error, "Could not write to " + Utf8(target) + ".");
    }
    const ExtractResult r = disc.ExtractAll(target, progress, error);
    if (r == ExtractResult::kOk && !fs::remove(marker, ec)) {
      return gow3::setup::Fail(error, "Could not remove " + Utf8(marker) + ".");
    }
    return r;
  });
}

std::string SwapFailure(SwapResult r, Build to) {
  if (r == SwapResult::kGameRunning) return kCloseFirst;
  return std::string("Could not switch gow3.exe to the ") + (to == Build::kTu6 ? "Title Update 6" : "base") +
         " build (is builds\\gow3-" + (to == Build::kTu6 ? "tu6" : "base") + ".exe there?).";
}

// Waits for gow3.exe to close, then swaps the build. kFailed (with the message) when it can't.
ExtractResult WaitAndSwap(const fs::path& dir, Build to, const Progress& progress, std::string* error) {
  const ExtractResult w = WaitForGame(dir / "gow3.exe", progress);
  if (w == ExtractResult::kFailed) return gow3::setup::Fail(error, kCloseFirst);
  if (w != ExtractResult::kOk) return w;
  const SwapResult s = gow3::setup::SwapBuild(dir, to, std::chrono::seconds(0));
  return s == SwapResult::kOk ? ExtractResult::kOk : gow3::setup::Fail(error, SwapFailure(s, to));
}

// Installing a TU on the base build ends in a swap to builds\gow3-tu6.exe: say so before any work
// starts when that file isn't there (a developer build folder, or a damaged release).
bool Tu6BuildMissing(const App& app, bool tu_ticked) {
  std::error_code ec;
  if (!tu_ticked || app.state.build != Build::kBase ||
      fs::is_regular_file(app.dir / "builds" / "gow3-tu6.exe", ec)) {
    return false;
  }
  g.message = "Title Update 6 needs builds\\gow3-tu6.exe next to the launcher, and it isn't there (this "
              "folder isn't a complete release). Untick Title Update 6, or use the release package.";
  return true;
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
      const ExtractResult w = WaitForGame(dir / "gow3.exe", progress);
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

void OfferSection(const char* title, bool tu, const char* none);

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
  ImGui::TextUnformatted(Utf8(g.iso->file.filename()).c_str());
  const std::string where = kDot + Gb(g.iso->size) + " GB";
  const float avail = ImGui::GetContentRegionAvail().x;
  const std::string folder = FitLeft(Utf8(g.iso->file.parent_path()), avail - ImGui::CalcTextSize(where.c_str()).x);
  ImGui::TextColored(kMuted, "%s%s", folder.c_str(), where.c_str());
  ImGui::TextColored(kMuted, "%s", Ids(g.iso->xex.title_id, g.iso->xex.media_id).c_str());
  EndBox();

  ImGui::Dummy(ImVec2(0, Dp(2.0f)));
  ImGui::TextColored(kMuted, "Extract it to");
  const float browse_w = ImGui::CalcTextSize("Browse").x + Dp(28.0f), gap = Dp(6.0f);
  BeginBox("##target", ImGui::GetContentRegionAvail().x - browse_w - gap);
  ImGui::TextUnformatted(FitLeft(Utf8(g.target), ImGui::GetContentRegionAvail().x).c_str());
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

  uint64_t needed = g.disc->TotalBytes();
  for (const OfferItem& i : g.items) {
    if (i.usable && i.ticked && !i.pkg.is_tu) needed += i.pkg.size;  // DLC is copied into the game folder
  }
  const bool enough = g.free_bytes && *g.free_bytes >= needed;
  std::string space = "Needs " + Gb(needed) + " GB free";
  if (g.free_bytes) space += " (" + g.drive + " has " + Gb(*g.free_bytes) + " GB)";
  space += ". Your ISO is not changed.";
  ImGui::PushFont(nullptr, theme::kSmallSize);
  ImGui::TextColored(enough ? kMuted : kAccent, "%s", space.c_str());
  if (g.target_in_iso) ImGui::TextColored(kAccent, "Pick a folder outside the ISO file.");
  ImGui::PopFont();
  if (!g.message.empty()) ImGui::TextColored(kAccent, "%s", g.message.c_str());

  // Title Update 6 and DLC, installed after the game (base, then TU6, then DLC). Optional.
  ImGui::Dummy(ImVec2(0, Dp(4.0f)));
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(kMuted, "%s",
                     "Also install Title Update 6 or DLC? Tick what you want; Add files picks the package files, "
                     "or a folder that holds them (unpack any .zip, .7z or .rar first). DLC that needs Title "
                     "Update 6 only shows up in the game with it.");
  ImGui::PopTextWrapPos();
  const float list_h = ImGui::GetContentRegionAvail().y - Dp(40.0f) - Dp(20.0f);
  ImGui::BeginChild("##setup_list", ImVec2(0, std::max(list_h, Dp(60.0f))));
  OfferSection("TITLE UPDATE 6", true, "None found (optional).");
  OfferSection("DLC", false, "None found (optional).");
  ImGui::EndChild();

  const bool can_extract = enough && !g.target_in_iso;
  const char* kAdd = "Add files…";
  const std::string_view b =
      app.game.empty()
          ? BottomButtons({{"Pick another file"}, {kAdd}, {"EXTRACT", true, can_extract}})
          : BottomButtons({{"Back"}, {"Pick another file"}, {kAdd}, {"EXTRACT", true, can_extract}});
  if (b == "Back") Show(View::kOverview);
  if (b == "Pick another file") {
    g.message.clear();
    HandlePicked(app, PickIsoOrFolder(app.hwnd));
  }
  if (b == kAdd) AddPicked(app, PickPackageOrFolder(app.hwnd));
  const bool tu_ticked =
      std::any_of(g.items.begin(), g.items.end(), [](auto& i) { return i.usable && i.ticked && i.pkg.is_tu; });
  if (b == "EXTRACT" && !Tu6BuildMissing(app, tu_ticked)) StartExtract();
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
    ImGui::PushFont(nullptr, theme::kSmallSize);
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

// One offer row: tick box, name, then the file and its folder, then the IDs. Click toggles it.
void DrawOfferItem(OfferItem& item) {
  using namespace theme;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float w = ImGui::GetContentRegionAvail().x, line = ImGui::GetTextLineHeight();
  ImGui::PushFont(nullptr, kSmallSize);
  const float small_h = ImGui::GetTextLineHeight();
  ImGui::PopFont();
  const float row_h = line + small_h * 2 + Dp(12.0f);
  ImGui::BeginDisabled(!item.usable);
  if (ImGui::InvisibleButton("##tick", ImVec2(w, row_h))) item.ticked = !item.ticked;
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float box = Dp(15.0f);
  const ImVec2 b0(p.x, p.y + Dp(3.0f) + (line - box) * 0.5f), b1(b0.x + box, b0.y + box);
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
  const float x = b1.x + Dp(10.0f), text_w = p.x + w - x;
  std::string name, extra;
  if (item.pkg.is_tu) {
    name = "Title Update 6";
    if (!item.usable) extra = " (for another disc version, can't be installed)";
  } else {
    name = item.pkg.header.display_name.empty() ? Utf8(item.pkg.file.filename()) : item.pkg.header.display_name;
  }
  const ImVec2 t(x, p.y + Dp(3.0f));
  dl->AddText(t, ImGui::GetColorU32(item.usable ? kText : kMuted), name.c_str());
  if (!extra.empty()) {
    dl->AddText(ImVec2(t.x + ImGui::CalcTextSize(name.c_str()).x, t.y), ImGui::GetColorU32(kMuted), extra.c_str());
  }
  ImGui::PushFont(nullptr, kSmallSize);
  const std::string file = Utf8(item.pkg.file.filename()) + kDot;
  const std::string where =
      file + FitLeft(Utf8(item.pkg.file.parent_path()), text_w - ImGui::CalcTextSize(file.c_str()).x);
  dl->AddText(ImVec2(x, t.y + line + Dp(2.0f)), ImGui::GetColorU32(kMuted), where.c_str());
  const std::string ids = Ids(item.pkg.header.title_id, item.pkg.header.media_id) + kDot + Size(item.pkg.size);
  dl->AddText(ImVec2(x, t.y + line + small_h + Dp(3.0f)), ImGui::GetColorU32(kMuted), ids.c_str());
  ImGui::PopFont();
  ImGui::EndDisabled();
}

void OfferSection(const char* title, bool tu, const char* none) {
  using namespace theme;
  ImGui::PushFont(nullptr, kGroupSize);
  ImGui::TextColored(kAccent, "%s", title);
  ImGui::PopFont();
  bool any = false;
  int index = tu ? 0 : 10000, other_disc = 0;
  for (OfferItem& item : g.items) {
    if (item.pkg.is_tu != tu) continue;
    if (!item.usable) {  // a TU for another disc version: counted on one line below, not listed
      ++other_disc;
      continue;
    }
    ImGui::PushID(index++);
    DrawOfferItem(item);
    ImGui::PopID();
    any = true;
  }
  ImGui::PushFont(nullptr, kSmallSize);
  if (!any) ImGui::TextColored(kMuted, "%s", none);
  if (other_disc) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kMuted, "Also found %d Title Update file%s for other versions of the disc (not your Media ID), "
                       "so they can't be used.", other_disc, other_disc == 1 ? "" : "s");
    ImGui::PopTextWrapPos();
  }
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, Dp(6.0f)));
}

void DrawOffer(App& app) {
  using namespace theme;
  PageTitle("TITLE UPDATE 6 AND DLC");
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(kMuted, "%s",
                     g.items.empty()
                         ? "None were found next to your game or the launcher. Use Add files to pick the package "
                           "files, or a folder that holds them. Unpack any .zip, .7z or .rar first: archives are "
                           "not searched."
                         : "Tick what to install. Not listed? Use Add files to pick the package files, or a folder "
                           "that holds them. Unpack any .zip, .7z or .rar first: archives are not searched.");
  ImGui::PopTextWrapPos();
  ImGui::Dummy(ImVec2(0, Dp(4.0f)));
  const float list_h = ImGui::GetContentRegionAvail().y - Dp(40.0f) - Dp(54.0f);
  ImGui::BeginChild("##offer_list", ImVec2(0, std::max(list_h, Dp(80.0f))));
  if (app.state.build == Build::kBase) {
    OfferSection("TITLE UPDATE 6", true, "No Title Update 6 package found (optional; some DLC only shows up in the game with it).");
  }
  OfferSection("DLC", false, "No DLC package found.");
  ImGui::EndChild();
  ImGui::PushFont(nullptr, kSmallSize);
  ImGui::TextColored(kMuted, "%s", kNoteSaves);
  ImGui::PopFont();
  if (!g.message.empty()) ImGui::TextColored(kAccent, "%s", g.message.c_str());

  const bool any = std::any_of(g.items.begin(), g.items.end(), [](auto& i) { return i.usable && i.ticked; });
  const bool listed = std::any_of(g.items.begin(), g.items.end(), [](auto& i) { return i.usable; });
  const char* keep = !listed ? "Back" : app.state.build == Build::kBase ? "Keep base game" : "Not now";
  const std::string_view b = BottomButtons({{"Add files\xE2\x80\xA6"}, {keep}, {"INSTALL", true, any}});
  if (b == "Add files\xE2\x80\xA6") {
    AddPicked(app, PickPackageOrFolder(app.hwnd));
  } else if (b == keep) {
    ContentAsked(app);
    for (const OfferItem& i : g.items) {
      if (i.usable) app.state.declined.insert(i.pkg.id);
    }
    SaveState(app);
    FlowDone(app, g.message);
  } else if (b == "INSTALL") {
    std::vector<FoundPackage> picked;
    g.offered.clear();
    g.install_names.clear();
    for (const OfferItem& i : g.items) {
      if (!i.usable) continue;
      g.offered.push_back(i.pkg.id);
      if (!i.ticked) continue;
      picked.push_back(i.pkg);
      g.install_names += (g.install_names.empty() ? "" : kDot) + ItemName(i);
    }
    const bool tu_ticked = std::any_of(picked.begin(), picked.end(), [](auto& p) { return p.is_tu; });
    if (Tu6BuildMissing(app, tu_ticked)) return;
    ContentAsked(app);
    StartInstall(app, picked, JobKind::kInstall);
  }
}

// After setup, when nothing was found next to the game: ask before going to Play.
void DrawAskContent(App& app) {
  using namespace theme;
  PageTitle(g.ask_existing ? "NOW SUPPORTED: TITLE UPDATE 6 AND DLC" : "TITLE UPDATE 6 AND DLC");
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(g.ask_existing
                             ? "Title Update 6 and DLC (RAAM's Shadow, the map packs) are now supported. Do you "
                               "want to install them?"
                             : "Your game is set up. Do you also have Title Update 6 or DLC (RAAM's Shadow, the "
                               "map packs) for it?");
  ImGui::Dummy(ImVec2(0, Dp(4.0f)));
  ImGui::PushFont(nullptr, kSmallSize);
  ImGui::TextColored(kMuted, "%s",
                     "They are your own packages, for example copied from your Xbox 360. Put them in a folder "
                     "next to the game; unpack any .zip, .7z or .rar first, since archives are not searched. Some "
                     "DLC only shows up in the game with Title Update 6. You can also add them later on the Game "
                     "files page.");
  ImGui::PopFont();
  ImGui::PopTextWrapPos();
  if (!g.message.empty()) ImGui::TextColored(kAccent, "%s", g.message.c_str());
  const char* no = g.ask_existing ? "Keep base game" : "No, just play";
  const std::string_view b = BottomButtons({{no}, {"YES", true}});
  if (!b.empty()) ContentAsked(app);
  if (b == no) {
    FlowDone(app);
    if (app.start == App::Start::kNormal) app.page = App::Page::kPlay;
  } else if (b == "YES") {
    if (!BuildOffer(app, true)) {
      Show(View::kOffer);
      AddPicked(app, PickPackageOrFolder(app.hwnd));
    } else {
      Show(View::kOffer);
    }
  }
}

void DrawRepair(App& app) {
  using namespace theme;
  PageTitle("TITLE UPDATE 6 MISSING");
  // The folders gow3.exe checks (gow3_tu6_folder, else next to gow3.exe, then in the game folder).
  std::string where;
  for (const fs::path& folder : Tu6Candidates(app)) where += (where.empty() ? "" : " or ") + Utf8(folder);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(kMuted, "Title Update 6 (default.xexp) is missing from %s", where.c_str());
  ImGui::PopTextWrapPos();
  ImGui::Dummy(ImVec2(0, Dp(4.0f)));
  BeginBox("##repair");
  ImGui::TextWrapped("Install it again from your Title Update 6 package, or switch back to the base game.");
  ImGui::PushFont(nullptr, theme::kSmallSize);
  ImGui::TextColored(kMuted, "The launcher looks for the package next to the game and next to itself.");
  if (!g_tu6_configured.empty()) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kMuted, "gow3_tu6_folder is set in gow3.toml: INSTALL IT AGAIN puts the update in %s, which "
                               "the game uses only when gow3_tu6_folder is empty.",
                       Utf8(app.game / "TU6").c_str());
    ImGui::PopTextWrapPos();
  }
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

// launcher.ini records a build whose builds\ copy is here, and gow3.exe is not that copy.
bool BuildReplaced(const App& app) {
  using gow3::setup::Build;
  if (!app.state.existed) return false;
  const fs::path exe = app.dir / "gow3.exe";
  const fs::path recorded =
      app.dir / "builds" / (app.state.build == Build::kTu6 ? "gow3-tu6.exe" : "gow3-base.exe");
  std::error_code ec;
  if (!fs::is_regular_file(exe, ec) || !fs::is_regular_file(recorded, ec)) return false;
  // The two builds differ in size, so that is enough on a plain start; after gow3.exe handed over
  // with --repair, compare the bytes too.
  if (app.start != App::Start::kRepair) {
    const uintmax_t a = fs::file_size(exe, ec), b = ec ? 0 : fs::file_size(recorded, ec);
    return !ec && a != b;
  }
  return !gow3::setup::SameFileContents(exe, recorded);
}

void DrawRestoreBuild(App& app) {
  using namespace theme;
  const bool tu6 = app.state.build == Build::kTu6;
  PageTitle("BUILD REPLACED");
  ImGui::Dummy(ImVec2(0, Dp(4.0f)));
  BeginBox("##restore");
  ImGui::TextWrapped(tu6 ? "Your Title Update 6 build was replaced (for example by extracting a new release over "
                           "this folder). Restore it?"
                         : "Your base-game build was replaced (for example by copying another gow3.exe into this "
                           "folder). Restore it?");
  ImGui::PushFont(nullptr, theme::kSmallSize);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(kMuted, "%s",
                     tu6 ? "The base game can't read saves made with Title Update 6. Restore keeps them working."
                         : "Restore copies builds\\gow3-base.exe to gow3.exe.");
  ImGui::PopTextWrapPos();
  ImGui::PopFont();
  EndBox();
  if (!g.message.empty()) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kAccent, "%s", g.message.c_str());
    ImGui::PopTextWrapPos();
  }
  const std::string_view b =
      tu6 ? BottomButtons({{"Use base game"}, {"RESTORE", true}}) : BottomButtons({{"RESTORE", true}});
  if (b == "RESTORE") StartSwitch(app, app.state.build);
  if (b == "Use base game") StartSwitch(app, Build::kBase);
}

void DrawOverview(App& app) {
  using namespace theme;
  PageTitle("GAME FILES");
  ImGui::Dummy(ImVec2(0, Dp(4.0f)));
  if (Row("Game folder", app.game.empty() ? std::string("Not set") : Utf8(app.game), false, "Change\xE2\x80\xA6")) {
    g.message.clear();
    HandlePicked(app, PickIsoOrFolder(app.hwnd));
    if (g.view != View::kOverview) return;
  }
  const bool tu6 = app.state.build == Build::kTu6;
  const bool can_switch = !app.game.empty() && (tu6 || Tu6Present(app));
  if (Row("Build", app.game.empty() ? "Not installed" : tu6 ? "Title Update 6" : "Base", false, "Switch",
          can_switch)) {
    StartSwitch(app, tu6 ? Build::kBase : Build::kTu6);
    return;
  }
  std::string content;
  if (Tu6Present(app)) content = "Title Update 6";
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
    BuildOffer(app, true);
    Show(View::kOffer);  // with nothing found: the empty lists and Add files
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
  const gow3::setup::SettingsFile toml = gow3::setup::LoadSettings(app.dir / "gow3.toml");
  g_play.renderer = RendererText(toml);
  const auto tu6 = toml.values.find("gow3_tu6_folder");
  g_tu6_configured = tu6 == toml.values.end() ? fs::path() : gow3::setup::FromUtf8(tu6->second);
}

void DrawPlayPage(App& app) {
  using namespace theme;
  const bool busy = g_job.Running();
  const bool ready = app.CanPlay() && !busy;
  PageTitle(ready ? "READY" : "SETUP NEEDED", ready ? kTitle : kAccent);
  std::string line = g_play.content;
  if (busy) line = "Wait for the Game files page to finish.";
  else if (app.game.empty()) line = "Choose your game folder on the Game files page.";
  else if (app.build_replaced) line = "gow3.exe was replaced: restore your build on the Game files page.";
  else if (!ready) line = "gow3.exe is missing from the launcher's folder.";
  ImGui::TextColored(kMuted, "%s", line.c_str());
  ImGui::Dummy(ImVec2(0, Dp(10.0f)));

  Row("Game folder", app.game.empty() ? std::string("Not set") : Utf8(app.game));
  Row("Build", app.game.empty() ? "NOT INSTALLED" : app.state.build == Build::kTu6 ? "TU6" : "BASE", true);
  Row("Renderer", g_play.renderer);

  const float button_h = Dp(54.0f);
  const float bottom = ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y;
  if (!app.error.empty()) {
    ImGui::SetCursorPosY(bottom - button_h - ImGui::GetTextLineHeightWithSpacing() - Dp(4.0f));
    ImGui::TextColored(kAccent, "%s", app.error.c_str());
  }
  ImGui::SetCursorPosY(bottom - button_h);
  ImGui::PushFont(HeadingFont(), 30.0f);
  if (app.game.empty() && !busy) {  // nothing installed: the button leads to setup
    if (PrimaryButton("SET UP GAME", ImVec2(0, button_h))) app.page = App::Page::kGameFiles;
  } else {
    ImGui::BeginDisabled(!ready);
    if (PrimaryButton("PLAY", ImVec2(0, button_h))) app.StartGameAndQuit();
    ImGui::EndDisabled();
  }
  ImGui::PopFont();
}

// ---------------------------------------------------------------------------------------------
// Game files page

void InitGameFiles(App& app) {
  using namespace gow3::setup;
  // Earlier launchers offered (and recorded) the disc's own DLC\AvatarAwards: forget it.
  bool cleaned = false;
  for (std::set<std::string>* ids : {&app.state.installed, &app.state.declined}) {
    cleaned |= std::erase_if(*ids, [](const std::string& id) {
                 return IsDiscOwnPackage(FromUtf8(id.substr(0, id.rfind(':'))));
               }) > 0;
  }
  if (cleaned) SaveState(app);
  if (app.game.empty()) {
    const FindResult found = FindGame(SearchRoots(app.dir, 2));
    if (found.game) {
      if (SetGame(app, *found.game)) AfterGameSet(app);
    } else if (found.isos.empty() || !UseIso(app, found.isos.front().file)) {
      Show(View::kSetup);
    }
    return;
  }
  app.build_replaced = BuildReplaced(app);
  if (app.build_replaced) {
    Show(View::kRestoreBuild);
    app.page = App::Page::kGameFiles;
  } else if (app.start == App::Start::kRepair) {
    Show(View::kRepair);
  } else if (app.state.build == Build::kBase && !app.state.content_asked) {
    // An install from before TU6 / DLC support (opened by gow3.exe or by hand): ask once first;
    // YES searches and shows what was found.
    g.ask_existing = true;
    Show(View::kAskContent);
    app.page = App::Page::kGameFiles;
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
      if (!SetGame(app, g.target)) return;
      // A new game folder: what was recorded for an earlier one no longer applies.
      app.state.installed.clear();
      app.state.declined.clear();
      app.state.content_asked = true;  // TU6 / DLC were chosen on Set up
      SaveState(app);
      if (!g.pending.empty()) {
        const std::vector<FoundPackage> pending = std::move(g.pending);
        g.pending.clear();
        StartInstall(app, pending, JobKind::kInstall);  // then TU6, then DLC
      } else {
        FlowDone(app);
        if (app.start == App::Start::kNormal) app.page = App::Page::kPlay;
      }
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
      app.build_replaced = false;  // gow3.exe is now the builds\ copy of the recorded build
      app.state.build = g.switch_to;
      SaveState(app);
      RefreshPlayPage(app);
      // A restored TU6 build without its title update: the repair page, not a game that stops.
      if (g.switch_to == Build::kTu6 && !Tu6Present(app)) Show(View::kRepair, g.message);
      else FlowDone(app, g.message);
    } else {
      const View back = app.build_replaced ? View::kRestoreBuild
                        : app.start == App::Start::kRepair && app.state.build == Build::kTu6 && !Tu6Present(app)
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
  std::string done = g.message;
  if (g.kind == JobKind::kInstall && done.empty()) {
    done = "Installed: " + g.install_names + "." +
           (outcome->swap == SwapResult::kOk ? " gow3.exe is now the Title Update 6 build." : "");
  }
  FlowDone(app, done);  // after a manual install: the Game files page, saying what went in
}

void DrawGameFilesPage(App& app) {
  switch (g.view) {
    case View::kSetup: DrawSetup(app); break;
    case View::kWorking: DrawWorking(); break;
    case View::kAskContent: DrawAskContent(app); break;
    case View::kOffer: DrawOffer(app); break;
    case View::kRepair: DrawRepair(app); break;
    case View::kRestoreBuild: DrawRestoreBuild(app); break;
    case View::kOverview: DrawOverview(app); break;
  }
}

bool GameFilesBusy() { return g_job.Running(); }

void ShutdownGameFiles() { g_job.Stop(); }
