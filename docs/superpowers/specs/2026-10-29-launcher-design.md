# Launcher: first-run setup, Title Update 6 / DLC upgrade, settings, updates

Date: 2026-10-29. Status: design approved in brainstorming, spec awaiting review.

## Goal

A player downloads the release, runs `gow3.exe`, and the game sets itself up from their own files:
it finds or extracts their Gears of War 3 ISO, offers their own Title Update 6 and DLC when it sees
them, lets them pick settings before the first launch, and offers new releases. Nothing from the game
is ever shipped; the player brings their own copy (README "Bring your own game").

## Decisions (from brainstorming)

- Players usually have an ISO. The launcher extracts it to a folder they choose and uses that folder
  as the game path. An already extracted game (`default.xex` + `GearGame`) is used as it is.
- One executable name: `gow3.exe` is always the game. The release ships both prebuilt games and the
  launcher swaps the right one in as `gow3.exe` (players never compile anything).
- A separate `launcher.exe` does setup, upgrades, settings and release updates.
- Title Update 6 and DLC are optional. When they appear next to the game later, the player is asked
  once; "Keep base game" is remembered and never asked again (upgrade later from the launcher).
- UI: Dear ImGui, dark, minimal, sidebar layout (mockup A), Teko headings, crimson accent (#D23B3B).
- Settings tabs are the F10 menu's own pages (same option catalog, same `gow3.toml`).
- Existing `gow3.toml` settings are loaded and kept; only changed keys are written.
- The launcher checks GitHub for a newer release and asks before updating.

## Release layout

```
gow3.exe                 the game (a copy of one of the builds below)
launcher.exe             setup, upgrades, settings, updates
builds\gow3-base.exe     disc executable build
builds\gow3-tu6.exe      Title Update 6 build
gow3.toml                settings (kept across updates)
gow3_game_path.txt       game folder (written by the launcher, read by the game)
launcher.ini             launcher state (kept across updates)
rexruntime.dll, rexgpu-xenos.dll, FidelityFX DLLs, VC++ runtime, assets\ ...
```

Both builds load the same DLLs from the release folder (`builds\` holds only the two copies the
launcher swaps in, never run from there). The zip grows by about 45 MB (one more exe, compressed).

`launcher.ini` keys: `build = base|tu6`; `declined = <package id>,...`; `installed = <package id>,...`;
`skip_release = <tag>`; `check_updates = true|false`. A package id is its file name plus size.

## Components

| Unit | Does | Depends on |
|------|------|------------|
| `launcher/main.cpp` | window, sidebar, page routing, Play | ImGui, SDL3 (from the SDK), pages |
| `launcher/finder.cpp` | finds ISO, extracted game, TU / DLC packages; identifies them by header | `xdvdfs`, `stfs` |
| `launcher/xdvdfs.cpp` | reads an Xbox 360 game disc image (XGD2/XGD3 game partition, XDVDFS directory tree) and extracts files with progress and cancel | none |
| `launcher/stfs.cpp` | reads STFS packages (`CON `/`LIVE`/`PIRS`): header fields, file table, extraction (port of `tools/stfs_extract.py`, with its multi-level hash table fix) | none |
| `launcher/install.cpp` | extract TU6 to `<game>\TU6`, copy DLC to `<game>\DLC`, swap `gow3.exe` | `stfs`, Windows file APIs |
| `launcher/update.cpp` | GitHub release check, download, SHA-256 check, file replacement | WinHTTP, BCrypt |
| `launcher/state.cpp` | `launcher.ini`, `gow3_game_path.txt` | none |
| settings pages | the F10 catalog (`gow3_options_catalog/model/values*`) and `gow3_toml_edit`, drawn by the launcher's ImGui page code | shared source files |
| game: `src/gow3_launch_check.cpp` | before the game starts: decide whether to hand over to the launcher | `finder` (shared), `state` |

`finder`, `stfs` and `state` are compiled into both the launcher and the game (the game needs them
for its start-up check). The settings catalog files are compiled into the launcher too; anything in
them that needs the running game (live apply) is skipped when no runtime exists.

## Flows

### Every `gow3.exe` start (game side, before the runtime starts)

1. Game path from `gow3_game_path.txt` (or `--game_data_root`) not valid: start `launcher.exe
   --setup`, exit.
2. Base build, and a TU6 or DLC package is found nearby (below) that is neither installed nor
   declined: start `launcher.exe --offer`, exit.
3. TU6 build and `<game>\TU6\default.xexp` missing: start `launcher.exe --repair`, exit (replaces
   today's "Title Update 6 missing" box).
4. TU6 build and a new DLC package found that is neither installed nor declined: `launcher.exe
   --offer`, exit.
5. Otherwise the game starts.

`gow3.exe` passes its own command-line arguments on to the launcher, which passes them back when it
starts the game again.

`--repair` opens the Game files page with "Title Update 6 is missing from <game>\TU6" and two
choices: install it again (package search below) or switch back to the base build.

Players upgrading from v0.2.2 or older have a `gow3.exe` and no `launcher.ini`: the launcher treats
that as `build = base` and keeps their `gow3_game_path.txt` if it is valid (no setup run).

### First run (`--setup`)

1. Search: the launcher's folder and up to two parent folders, each with its direct subfolders.
2. Extracted game found: save it as the game path.
3. ISO found: "Set up your game" page: ISO name, folder, size, **Title ID and Media ID**; an
   "Extract to" folder (default: a `Gears of War 3` folder next to the ISO; Browse opens the Windows
   folder picker); free-space check; Extract / Pick another file.
4. Nothing found: same page asking for an ISO or a game folder (Windows file / folder picker).
5. Extraction page: current file, progress bar, size done of total, time left, Cancel. Settings tabs
   stay usable during extraction.
6. Done: save the game path, then the package step below, then the Play page.

### Packages (`--offer`, and the Game files page)

1. Search the game folder, its parent, and the launcher's folders (and their direct subfolders) for
   STFS packages.
2. Keep Gears of War 3 ones (title `4D5308AB`): Title Update (content type `000B0000`) whose media ID
   matches the disc's (from `default.xex`), and DLC (content type `00000002`). Several TU files (one
   per disc version): only the matching one. Show each with its Title ID and Media ID.
3. "New content found" page: a tick box per package, the note "Your saves carry over. The base game
   can't read saves made after this.", Keep base game / Install.
4. Install: extract the TU to `<game>\TU6`; copy DLC packages to `<game>\DLC` (the game installs them
   itself at start-up, `src/gow3_dlc.cpp`); if TU6 is now present, swap in the TU6 build; record
   installed ids; start `gow3.exe`.
5. Keep base game: record the ids as declined; start `gow3.exe`. The Game files page always offers
   them again manually.

### Swapping the build

1. Wait (up to 30 s) for every `gow3.exe` process from this folder to exit.
2. Copy `builds\gow3-<build>.exe` to `gow3.exe.new`, then `MoveFileEx(gow3.exe.new, gow3.exe,
   MOVEFILE_REPLACE_EXISTING)`.
3. Record `build` in `launcher.ini`. The Game files page can switch back to base the same way.

### Release updates

1. On launcher start (unless `check_updates = false`), at most once a day, request
   `https://api.github.com/repos/Flippage/gears-of-war-3-recomp/releases/latest` (no account, no data
   sent besides the request). Pre-releases are ignored.
2. If its tag is newer than the launcher's own version (compiled in from the packaging version) and
   is not `skip_release`: "Version X is available" with its release notes, Update / Skip this version
   / Later.
3. Update: download `gow3-recomp-X-win64.zip` and its `.sha256` from that release; refuse if the hash
   does not match; extract to a temporary folder next to the release; replace every file inside the
   zip's top folder (`gow3-recomp-X-win64\`) except `gow3.toml`, `gow3_game_path.txt`, `launcher.ini`; the running `launcher.exe` is
   renamed to `launcher.old.exe` first and deleted on the next start; then copy the recorded build
   from the new `builds\` to `gow3.exe` again; restart the launcher.
4. Errors (offline, rate limited, hash mismatch, a file in use): say so, change nothing, offer Later.

## Settings

- The launcher reads the existing `gow3.toml` at start and shows its values. Only keys the player
  changes are written, through `gow3_toml_edit` (the F10 menu's editor), which keeps every other line,
  comment and unknown key. A missing file starts from the release's default `gow3.toml`.
- Updates never touch `gow3.toml`. Options added in a new release take their defaults until changed.
- Settings that need a restart in the game (renderer, resolution scale, widescreen) need nothing
  special in the launcher: they apply at the next launch.

## UI

- Window about 900 x 560, fixed size, borderless with its own title bar (drag, minimise, close).
- Colours: background #0E0F11, sidebar #121417, panels #15171A, lines #1C1F23, text #D7D9DC, muted
  #7B8088, accent #D23B3B, success #5FBF7A. Teko for headings, Segoe UI / the F10 menu's body font for
  text.
- Sidebar: GEARS 3, Play, Game files, separator, Display, Graphics, Controls, Network, Saves. The
  active item has a crimson left edge.
- Play page: READY / SETUP NEEDED, installed content (TU6, DLC), game folder, build, renderer, a large
  PLAY button. Every settings page has a PLAY button in its footer.
- Option lists match what the release supports: the upscaler choices never include DLSS (it never
  ships; the mockup's "DLSS" stands in for the shipped upscaler).
- Controls are the F10 menu's: dropdowns, switches, sliders, keycaps (click to rebind).
- Mockups: `.superpowers/brainstorm/` (layout A, setup flow, settings tabs).

## Errors and safety

- Never deletes or changes saves, the ISO, or the player's packages; writes only the chosen game
  folder, `<game>\TU6`, `<game>\DLC`, and the launcher's own files.
- Not a Gears of War 3 disc (wrong Title ID): clear message, nothing written.
- Not enough free space: shown before extracting; Extract disabled.
- Cancelled or failed extraction: files written so far stay; running it again overwrites them.
- A TU that does not match the disc's Media ID is listed as "for another disc version" and cannot be
  installed.
- Update downloads are checked against the release's SHA-256 file before anything is replaced.

## Testing

- Unit-style checks (a test executable): XDVDFS reader on a small synthetic image; STFS reader on a
  synthetic package with more than 170 blocks (the hash-table case); `launcher.ini` round trip; tag
  comparison; toml edit keeps unknown lines.
- On the user's machine: first run from the ISO; first run with an extracted game; upgrade offer with
  the user's TU6 + DLC; Keep base game remembered; switch back to base; settings round trip with an
  existing `gow3.toml` from v0.2.2; update flow against a test pre-release (or a local mock server).

## Out of scope

- GOD (Games on Demand) packages as the game source (can be added to the finder later).
- Title updates other than TU6.
- Shipping or downloading any game, update or DLC file.
- Non-Windows platforms.
