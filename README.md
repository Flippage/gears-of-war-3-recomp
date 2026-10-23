# Gears of War 3 — native PC recompilation (unofficial, work in progress)

Retail Xbox 360 **Gears of War 3** statically recompiled from the player's own `default.xex` to
native x64 code with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). This is not
emulation of the game's CPU code: the PowerPC executable is translated to C++ and compiled for
Windows. The GPU side still uses ReXGlue's Xenia-derived Xenos renderer (Direct3D 12).

The project's short name is `gow3-recomp`. It follows the layout and approach of the
[Gears of War 2 native PC port](https://github.com/santiquiroz/gears-of-war-2-pc-port) and the
[Gears of War: Judgment recompilation](https://github.com/OverkillLabs3/gears-judgement-recomp), and
reuses their open-source tools and runtime patches (see [Credits](#credits)).

> **You must own the game.** This repository contains **no game files** — no executable, no disc
> image, no extracted data, no generated code. Every player builds from **their own** copy of
> Gears of War 3. Do not open issues or pull requests that contain game files, and do not ask for
> them.

## Status

Built from the base disc executable only (title ID `4D5308AB`, disc version `0.0.0.11`, media ID
`58122AD6`). Title updates are not supported.

- [x] Boots, title screen, menus, profile sign-in
- [x] Campaign: loads into missions and plays (tested through the first chapters)
- [x] Saving and loading checkpoints, Continue, across game restarts
- [x] Controller (tested with a DualSense); keyboard through the runtime's `--mnk_mode=true`
- [x] Shader pipelines compiled off the draw thread and cached between runs; models and textures
      no longer pop in late or hitch while streaming
- [x] Smooth frame pacing (one present per game frame) and faster EDRAM clears, both from the
      Gears of War 2 port
- [x] Community game patches as settings: Unlock FPS (60 fps and above), Black Shading Fix, 16x AF,
      texture streaming pool, and optional AO / motion blur / bloom / lens flares / depth of field /
      post-processing / shadow cuts toggles (off by default: the game runs well with its original look)
- [x] DLSS temporal upscaling calibrated for Gears 3 (camera, depth convention, camera cuts), with
      the HUD, subtitles and chapter titles masked out so they do not warp
- [x] Faces render cleanly in in-engine cutscenes (the black blotches are fixed, see
      [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md))
- [ ] FSR 3.1 / FSR 4 on AMD cards: built in and calibrated the same way, but untested
- [ ] Full campaign play-through; Horde, Beast and Versus untested
- [ ] PC options menu, mouse aim, ultrawide

There is no binary release. Build it yourself as described below.

## Bring your own game

1. Dump **your own** Gears of War 3 disc (base version, no title update) to an ISO.
2. Extract it to a folder **outside this repository**, for example with
   [xdvdfs](https://github.com/antangelo/xdvdfs):

   ```powershell
   xdvdfs unpack "<your Gears of War 3 ISO>" C:\Games\Gears3-GameFiles
   ```

   The folder must contain `default.xex` and the `GearGame` folder.
3. Copy that `default.xex` to `game\default.xex` in this repository. The `game\` folder, every
   game file type and everything generated from the game are git-ignored on purpose.

## Building from source

See [BUILDING.md](BUILDING.md). In short (Windows 10/11 x64, Visual Studio 2022+ with the C++
workload, LLVM/clang 18+, CMake, Ninja, Python 3):

```powershell
# Gears 3's own runtime DLLs (ReXGlue SDK + sdk\patches, with DLSS/FSR), first run ~15-20 min
powershell -ExecutionPolicy Bypass -File scripts\build-sdk.ps1
# The game itself, from your game\default.xex (~5 min)
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Sdk C:\path\to\rexglue-sdk\win-amd64
```

The result is `out\build\release\gow3.exe`.

## Running

Run `out\build\release\gow3.exe`. It finds your game folder through, in order:
`--game_data_root=<folder>`, a `gow3_game_path.txt` next to the exe (first line = the folder), or
the exe's folder and up to four parent folders (and their subfolders) holding `default.xex` and
`GearGame`.

Saves, the profile and the shader cache are written to `%LOCALAPPDATA%\GearsOfWar3PC`. The game
folder is never modified.

## Settings

`gow3.toml` next to `gow3.exe` (copied from [config/gow3.toml](config/gow3.toml) on the first
build; your edits are kept). Every setting also works on the command line as `--name=value`.

| Setting | Default | What it does |
|---|---|---|
| `gow3_unlock_fps` | on | Removes the engine's 30 fps cap |
| `gow3_black_shading_fix` | on | `GUseTilingMode = FALSE`; removes MSAA resolve artifacts |
| `gow3_disable_motion_blur` | on | Removes motion blur (ghosting when panning) |
| `gow3_disable_ao`, `gow3_disable_bloom`, `gow3_disable_lens_flares` | off | Community patches that trade the original look for speed |
| `gow3_disable_dof`, `gow3_occlusion_query_fix` | off | Community quality patches |
| `gow3_aniso16` | on | 16x anisotropic filtering |
| `gow3_resolution_scaling_fix` | off | Disables all post-processing (changes the look) |
| `gow3_shadow_perf_b` | off | Performance Patch Option B (smart shadow cuts) |
| `gow3_texture_pool_mb` | 196 | Texture streaming pool (the game's own is 145) |
| `readback_memexport` | off | CPU readback of memexport (on = long load stalls) |
| `gow2_temporal`, `gow2_upscaler` | off | Temporal upscaling: set `gow2_temporal = true` and `gow2_upscaler = "dlss"` (NVIDIA RTX), `"fsr4"`, `"fsr3"` or `"auto"` |
| `gow2_ui_mask` | on | Keeps the upscaler from warping the HUD and subtitles |

## Known issues

- The first launch after updating, or after changing a graphics setting, recompiles the shader
  pipelines: the window can show "Not responding" for a few minutes. Let it finish; later launches
  load them from the cache.
- With an upscaler on, characters that move by themselves can ghost slightly (only camera motion
  vectors are available).

## Finding the functions codegen misses

`tools/harvest_hints.py` (from the Gears 2 port) scans the decrypted image for function entries
that codegen missed; the image comes from `gow3.exe` itself (`GOW3_DUMP_IMAGE=<folder>`) and
`tools/make_image_blocks.py`. `scripts/harvest-loop.ps1` boots the game, reads the "unregistered
function" FATAL, adds it to `hints/runtime.toml` and rebuilds. `tools/patch_switch_traps.py` turns
codegen's give-up points into runtime dispatch. `tools/find_site.py` maps a guest address to the
generated C++.

## Credits

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) (BSD-3-Clause, derived from Xenia):
  recompiler and runtime.
- [Gears of War 2 native PC port](https://github.com/santiquiroz/gears-of-war-2-pc-port)
  (BSD-3-Clause): SDK patch series `0002`-`0029`, build scripts, the hint harvester, the UTF-8 and
  XamInput guards, and the overall approach.
- [Gears of War: Judgment recompilation](https://github.com/OverkillLabs3/gears-judgement-recomp)
  (BSD-3-Clause): the switch-trap patcher, the guards and the temporal upscaler code (by way of
  the Gears 2 port).
- [Xenia](https://github.com/xenia-canary/xenia-canary) (BSD-3-Clause): GPU fixes ported as
  `sdk/patches/0030` and `0031`, and the reference for correct rendering.
- Community game patches from
  [xenia-canary/game-patches](https://github.com/xenia-canary/game-patches) by **illusion**,
  **boma** and **Sowa_95**.
- NVIDIA DLSS SDK and AMD FidelityFX SDK: fetched at build time for your own use, never committed.
- [xdvdfs](https://github.com/antangelo/xdvdfs) (MIT): disc extraction.

Exact files, copyright lines and license texts: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Legal

Unofficial project. Not affiliated with or endorsed by Microsoft, Xbox Game Studios, The Coalition,
or Epic Games. Gears of War is a trademark of Microsoft Corporation; Gears of War 3 is © Microsoft
Corporation (originally © 2011 Epic Games, Inc.). All trademarks belong to their owners.

This repository contains no part of the game and must never be used to distribute it. You need
your own legally obtained copy. This repository's own code is licensed under the BSD 3-Clause
License — see [LICENSE](LICENSE); files from other projects keep their licenses
([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
