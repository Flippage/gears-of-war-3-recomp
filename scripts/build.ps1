<#
.SYNOPSIS
  Builds gow3 (Gears of War 3, PC) from your own game\default.xex.
  Modelled on santiquiroz/gears-of-war-2-pc-port scripts\build.ps1.

.PARAMETER Sdk
  ReXGlue SDK folder (contains bin\rexglue.exe). Or set REXGLUE_SDK.
.PARAMETER SkipCodegen
  Reuse the existing generated\ output.
#>
param(
    [string]$Sdk = $env:REXGLUE_SDK,
    [ValidateSet('Release', 'RelWithDebInfo', 'Debug')][string]$Config = 'Release',
    [switch]$SkipCodegen
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

function Fail($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }
function Step($msg) { Write-Host "`n== $msg" -ForegroundColor Cyan }

$rexglue = Join-Path $Sdk 'bin\rexglue.exe'
if (-not $Sdk) { Fail 'Give the ReXGlue SDK folder with -Sdk <folder> or set REXGLUE_SDK.' }
if (-not (Test-Path $rexglue)) { Fail "rexglue.exe not found in $Sdk\bin." }
if (-not (Test-Path 'game\default.xex')) { Fail 'game\default.xex is missing.' }

$env:PATH = [Environment]::GetEnvironmentVariable('PATH', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('PATH', 'User')
foreach ($tool in 'cmake', 'ninja') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { Fail "$tool was not found on PATH." }
}

Step 'Locating the C++ toolchain'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -all -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath |
    Where-Object { Get-ChildItem (Join-Path $_ 'VC\Tools\MSVC\*\include\excpt.h') -ErrorAction SilentlyContinue } |
    Select-Object -First 1
if (-not $vsPath) { Fail 'No Visual Studio install with a complete MSVC toolset (excpt.h) was found.' }
cmd /c "`"$vsPath\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
}
$llvm = Join-Path $env:ProgramFiles 'LLVM\bin'
if (-not (Test-Path "$llvm\clang++.exe")) { Fail 'clang++ was not found (install LLVM).' }
$env:PATH = "$llvm;$env:PATH"
New-Item -ItemType Directory -Force 'out\logs' | Out-Null

if (-not $SkipCodegen) {
    Step 'Generating C++ from default.xex'
    & $rexglue --log-file out\logs\codegen.log codegen gow3_manifest.toml
    if ($LASTEXITCODE -ne 0) { Fail 'rexglue codegen failed (see out\logs\codegen.log).' }
}

# Codegen gives up on under-counted jump tables with __builtin_trap(); dispatch through ctr instead.
# Idempotent, so it also runs with -SkipCodegen.
Step 'Patching switch traps in generated code'
python -I tools\patch_switch_traps.py 'generated/default/*.cpp'
if ($LASTEXITCODE -ne 0) { Fail 'tools\patch_switch_traps.py failed.' }

Step 'Applying the game patches'
python -I tools\patch_game_patches.py 'generated/default/*.cpp'
if ($LASTEXITCODE -ne 0) { Fail 'tools\patch_game_patches.py failed.' }

$build = Join-Path $root "out\build\$($Config.ToLower())"
Step "Configuring ($Config)"
cmake -S . -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config" `
    "-DCMAKE_C_COMPILER=$llvm\clang.exe" "-DCMAKE_CXX_COMPILER=$llvm\clang++.exe" `
    "-DCMAKE_LINKER=$llvm\lld-link.exe" "-DCMAKE_PREFIX_PATH=$Sdk" | Out-File out\logs\configure.txt -Encoding utf8
if ($LASTEXITCODE -ne 0) { Fail 'CMake configure failed (see out\logs\configure.txt).' }

# CMake re-runs codegen during the build when its inputs (the manifest, hints, the xex) are newer
# than its stamp, as in a fresh checkout. That would replace the generated files after the patch
# steps above and drop their fixes (an unresolved branch trap then stops the game at boot), so run
# that codegen target first and patch again before building.
Step 'Codegen target (only runs when its inputs changed)'
cmake --build $build --target gow3_codegen
if ($LASTEXITCODE -ne 0) { Fail 'The codegen target failed.' }
python -I tools\patch_switch_traps.py 'generated/default/*.cpp'
if ($LASTEXITCODE -ne 0) { Fail 'tools\patch_switch_traps.py failed.' }
python -I tools\patch_game_patches.py 'generated/default/*.cpp'
if ($LASTEXITCODE -ne 0) { Fail 'tools\patch_game_patches.py failed.' }

Step 'Building'
cmake --build $build --target gow3
if ($LASTEXITCODE -ne 0) { Fail 'Build failed.' }

# The runtime DLLs the exe loads (rexgpu-xenos.dll is loaded by name as the GPU plugin).
# out\sdk\ holds patched SDK DLLs when present (the Gears of War 2 port's sdk\patches series, built
# by its scripts\build-sdk.ps1: ROV on NVIDIA, no pipeline stalls, pipeline library, DLSS/FSR);
# the exe is compiled against the stock SDK headers either way, as the Gears 2 port does.
foreach ($dll in 'TracyClient.dll', 'rexruntime.dll', 'rexgpu-xenos.dll') {
    $src = Join-Path $Sdk "bin\$dll"
    if (Test-Path $src) { Copy-Item $src $build -Force }
}
$patched = Join-Path $root 'out\sdk'
if (Test-Path (Join-Path $patched 'rexruntime.dll')) {
    Copy-Item (Join-Path $patched '*.dll') $build -Force
    Write-Host "Patched SDK DLLs from $patched"
}
if (-not (Test-Path (Join-Path $build 'gow3.toml'))) {
    Copy-Item (Join-Path $root 'config\gow3.toml') $build
    Write-Host "Default settings: $build\gow3.toml"
}
if (Test-Path (Join-Path $root 'assets\glyphs')) {
    $glyphDst = Join-Path $build 'assets\glyphs'
    New-Item -ItemType Directory -Force $glyphDst | Out-Null
    Copy-Item (Join-Path $root 'assets\glyphs\*') $glyphDst -Recurse -Force
    Write-Host "Glyphs copied: $glyphDst"
}
if (Test-Path (Join-Path $root 'assets\fonts')) {
    $fontDst = Join-Path $build 'assets\fonts'
    New-Item -ItemType Directory -Force $fontDst | Out-Null
    Copy-Item (Join-Path $root 'assets\fonts\*') $fontDst -Recurse -Force
}
Write-Host "`nDone: $build\gow3.exe" -ForegroundColor Green

