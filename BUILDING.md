# Building gow3-recomp

## Requirements

| Requirement | Version / notes |
|---|---|
| OS | Windows 10/11 x64 |
| Visual Studio | 2022+ or Build Tools with "Desktop development with C++" and a Windows 10/11 SDK (the scripts pick an install whose MSVC toolset includes `excpt.h`) |
| clang / clang++ | 18+ ([LLVM](https://llvm.org) installed to `C:\Program Files\LLVM`, or the VS "C++ Clang tools for Windows" component); tested with 23.1.4 |
| CMake | 3.25+ |
| Ninja | any recent version |
| Python | 3.9+ |
| Git | for `scripts\build-sdk.ps1` |
| ReXGlue SDK | Windows x64 build, version `0.10.0.9-dev`, release tag [`nightly-20260921-923c1a59`](https://github.com/rexglue/rexglue-sdk/releases/tag/nightly-20260921-923c1a59) (`rexglue-sdk-0.10.0.9-dev.g923c1a5-win-amd64.zip`): the folder containing `bin\rexglue.exe` |
| Game executable | **your own** `default.xex` from the retail Gears of War 3 disc (see below) |

With winget: `winget install Kitware.CMake Ninja-build.Ninja LLVM.LLVM`.

## Your game executable

Only the base disc executable is supported: title ID `4D5308AB`, version `0.0.0.11`, media ID
`58122AD6`. Title updates have different code and are not supported.

1. Dump your own disc to an ISO and extract it **outside this repository**, e.g. with
   [xdvdfs](https://github.com/antangelo/xdvdfs):

   ```powershell
   xdvdfs unpack "<your Gears of War 3 ISO>" C:\Games\Gears3-GameFiles
   ```

2. Copy `default.xex` from that folder to `game\default.xex` here. `game\` is git-ignored, as is
   every game file type: **never commit game files.**

## Steps

1. **Runtime DLLs** (once, and again when `sdk\patches` changes):

   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts\build-sdk.ps1
   ```

   Clones the ReXGlue SDK at commit `923c1a5`, applies every `sdk\patches\*.patch`, fetches the
   NVIDIA DLSS SDK and the AMD FidelityFX SDK v2.3.0 (for your own use; nothing is committed),
   builds `rexruntime.dll` and `rexgpu-xenos.dll`, and installs them with `nvngx_dlss.dll` and the
   FidelityFX DLLs in `out\sdk\`. The first run downloads several GB and takes 10-20 minutes.

   - `-NoDlss`: build without the DLSS SDK.
   - `-Dev`: also build the frame trace tools (`gow2_trace.now`, `debug_skip_ps`).

2. **The game**:

   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Sdk C:\path\to\rexglue-sdk\win-amd64
   ```

   (or set `REXGLUE_SDK`). It runs `rexglue codegen gow3_manifest.toml` (about a minute, ~236 MB of
   C++ in `generated\`), patches the generated code (`tools\patch_switch_traps.py`,
   `tools\patch_game_patches.py`), configures with CMake (Ninja, clang), builds, and copies the
   runtime DLLs next to the exe: the SDK's own, or the patched ones from `out\sdk\` when present.
   `-SkipCodegen` reuses `generated\`.

3. Output: `out\build\release\gow3.exe`, plus a default `gow3.toml` copied from `config\` when none
   is there.

## What the code generator needs

`gow3_manifest.toml` drives codegen. It includes `hints\harvested.toml` (function entries found by
`tools\harvest_hints.py` in the decrypted image) and `hints\runtime.toml` (functions found by
booting, `scripts\harvest-loop.ps1`). Both hold only guest addresses.

To refresh the static hints: run the game once with `GOW3_DUMP_IMAGE=out\image` set (writes the
decrypted image), then

```powershell
python tools\make_image_blocks.py out\image
python tools\harvest_hints.py --image out\image --generated generated\default --out hints\harvested.toml --deny hints\denylist.txt
```

The image dump is game data: it stays in the git-ignored `out\`.
