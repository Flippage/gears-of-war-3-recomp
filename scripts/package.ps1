<#
.SYNOPSIS
  Packages an already-built gow3 tree into a Windows release zip (no game files, no DLSS).

.DESCRIPTION
  Copies gow3.exe, the release runtime DLLs (scripts\build-sdk.ps1 -NoDlss), AMD FidelityFX,
  the Visual C++ runtime, the button prompt glyphs, gow3.toml, the docs and a generated
  QUICKSTART.txt into a staging folder, and compresses it to
  <OutDir>\gow3-recomp-<Version>-win64.zip with a .sha256 file next to it.

  The zip never contains game data: packaging stops if default.xex or any *.xxx / *.tfc / *.bik
  / Coalesced*.bin / *.dds file would be staged. It never contains NVIDIA DLSS either: packaging
  stops if a staged .dll or .exe holds an NGX name (scripts\ngx-scan.ps1). DLSS stays a
  build-from-source feature (README, "DLSS").

  Adapted from the Gears of War 2 port's scripts\package.ps1 (BSD-3-Clause, Santiago Quiroz
  Upegui and the gow2-recomp contributors).

.PARAMETER Version
  Release version, e.g. "0.1.0". Used in the zip and top folder name.

.PARAMETER BuildDir
  Folder with the built gow3.exe. Default: out\build\release.

.PARAMETER SdkDir
  Folder with the release rexruntime.dll / rexgpu-xenos.dll and AMD FidelityFX DLLs from
  scripts\build-sdk.ps1 -NoDlss. Default: out\sdk-nodlss.

.PARAMETER OutDir
  Output folder for the zip and the staging area. Default: out\package.

.EXAMPLE
  .\scripts\build-sdk.ps1 -NoDlss
  .\scripts\package.ps1 -Version "0.1.0"
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [string]$BuildDir = 'out\build\release',

    [string]$SdkDir = 'out\sdk-nodlss',

    [string]$OutDir = 'out\package'
)

$ErrorActionPreference = 'Stop'

# Find-NgxBinaries: NVIDIA NGX names left in a built binary.
. (Join-Path $PSScriptRoot 'ngx-scan.ps1')

function Fail([string]$Message) {
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit 1
}

function Step([string]$Message) {
    Write-Host "`n== $Message" -ForegroundColor Cyan
}

function Resolve-RootedPath([string]$Root, [string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) { return $Path }
    return (Join-Path $Root $Path)
}

function Copy-Binaries([string]$FromBuild, [string]$FromSdk, [string]$StageDir) {
    $exe = Join-Path $FromBuild 'gow3.exe'
    if (-not (Test-Path $exe)) { Fail "gow3.exe not found in $FromBuild. Run scripts\build.ps1 first." }
    Copy-Item -Force $exe $StageDir
    foreach ($name in @('rexruntime.dll', 'rexgpu-xenos.dll')) {
        $src = Join-Path $FromSdk $name
        if (-not (Test-Path $src)) { Fail "$name not found in $FromSdk. Run scripts\build-sdk.ps1 -NoDlss first." }
        Copy-Item -Force $src $StageDir
    }
    $ffx = @(Get-ChildItem -Path $FromSdk -Recurse -File -Filter 'amd_fidelityfx_*.dll' -ErrorAction SilentlyContinue)
    if (-not $ffx.Count) {
        # rexruntime.dll imports amd_fidelityfx_dx12.dll, so the game does not start without it.
        $ffx = @(Get-ChildItem -Path $FromBuild -File -Filter 'amd_fidelityfx_*.dll')
    }
    if (-not ($ffx | Where-Object { $_.Name -ieq 'amd_fidelityfx_dx12.dll' })) {
        Fail 'amd_fidelityfx_dx12.dll not found (rexruntime.dll needs it).'
    }
    foreach ($f in $ffx) { Copy-Item -Force $f.FullName (Join-Path $StageDir $f.Name) }
}

function Find-VcRuntimeDir {
    $vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { Fail "vswhere.exe not found at $vswhere." }
    $needed = @('msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')
    $candidates = @()
    foreach ($install in (& $vswhere -all -products * -property installationPath)) {
        $msvcBase = Join-Path $install 'VC\Redist\MSVC'
        if (-not (Test-Path $msvcBase)) { continue }
        foreach ($ver in (Get-ChildItem -Path $msvcBase -Directory | Sort-Object Name -Descending)) {
            $crt = Get-ChildItem -Path (Join-Path $ver.FullName 'x64') -Directory -Filter 'Microsoft.VC14*.CRT' -ErrorAction SilentlyContinue
            foreach ($c in $crt) { $candidates += $c.FullName }
        }
    }
    foreach ($dir in ($candidates | Sort-Object -Descending)) {
        if (-not ($needed | Where-Object { -not (Test-Path (Join-Path $dir $_)) })) { return $dir }
    }
    Fail 'Visual C++ runtime DLLs not found under VC\Redist\MSVC\<ver>\x64\Microsoft.VC14*.CRT.'
}

function Copy-VcRuntime([string]$StageDir) {
    $crtDir = Find-VcRuntimeDir
    Write-Host "VC++ redist: $crtDir"
    foreach ($dll in @('msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')) {
        Copy-Item -Force (Join-Path $crtDir $dll) $StageDir
    }
}

function Copy-Content([string]$Root, [string]$StageDir) {
    foreach ($name in @('LICENSE', 'THIRD_PARTY_NOTICES.md', 'README.md')) {
        Copy-Item -Force (Join-Path $Root $name) $StageDir
    }
    Copy-Item -Force (Join-Path $Root 'config\gow3.toml') (Join-Path $StageDir 'gow3.toml')
    $glyphs = Join-Path $StageDir 'assets\glyphs'
    New-Item -ItemType Directory -Force -Path $glyphs | Out-Null
    Copy-Item -Recurse -Force (Join-Path $Root 'assets\glyphs\*') $glyphs
    $licenses = Join-Path $StageDir 'licenses'
    New-Item -ItemType Directory -Force -Path $licenses | Out-Null
    Set-Content -Path (Join-Path $licenses 'AMD-FidelityFX-MIT.txt') -Encoding ASCII -Value @(
        'AMD FidelityFX SDK (amd_fidelityfx_*.dll) - MIT License',
        'https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK',
        '',
        'Copyright (c) Advanced Micro Devices, Inc. All rights reserved.',
        '',
        'Permission is hereby granted, free of charge, to any person obtaining a copy',
        'of this software and associated documentation files (the "Software"), to deal',
        'in the Software without restriction, including without limitation the rights',
        'to use, copy, modify, merge, publish, distribute, sublicense, and/or sell',
        'copies of the Software, and to permit persons to whom the Software is',
        'furnished to do so, subject to the following conditions:',
        '',
        'The above copyright notice and this permission notice shall be included in',
        'all copies or substantial portions of the Software.',
        '',
        'THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR',
        'IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,',
        'FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE',
        'AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER',
        'LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,',
        'OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN',
        'THE SOFTWARE.'
    )
}

function Write-Quickstart([string]$StageDir, [string]$Version) {
    Set-Content -Path (Join-Path $StageDir 'QUICKSTART.txt') -Encoding ASCII -Value @(
        "Gears of War 3 - unofficial PC recompilation (gow3-recomp) $Version - QUICKSTART",
        '',
        'WHAT THIS IS',
        'An unofficial native PC build of Gears of War 3 (static recompilation, not an emulator).',
        'No game data is included: you need your own copy of the game.',
        '',
        'YOU NEED YOUR OWN COPY',
        'Dump your own Gears of War 3 disc (base version, no title update) and extract it to a folder',
        'that contains default.xex and the GearGame folder, for example with xdvdfs:',
        '  https://github.com/antangelo/xdvdfs',
        '',
        'RUNNING',
        'Tell gow3.exe where the game is, any one of these:',
        '  - put this folder inside or next to the game folder, or',
        '  - write the game folder path on the first line of gow3_game_path.txt next to gow3.exe, or',
        '  - run gow3.exe --game_data_root="C:\path\to\game"',
        'The first launch compiles shaders and can show "Not responding" for a few minutes. Let it finish.',
        '',
        'CONTROLS',
        'A controller works out of the box. For keyboard and mouse, press F10 (PC Options), Controls,',
        'and turn on Keyboard & Mouse Input and Mouse Look & Aim. WASD move, mouse aims, left click',
        'fires, right click aims down sights, Space cover, E use, R reload, F melee, 1-4 weapons,',
        'Enter or Esc pause. Rebind anything on the Keyboard & Mouse Bindings page.',
        '',
        'SETTINGS AND SAVES',
        'F10 opens the PC Options menu. Settings live in gow3.toml next to the exe.',
        'Saves and the shader cache are in %LOCALAPPDATA%\GearsOfWar3PC.',
        '',
        'UPSCALING',
        'FSR 3.1 / FSR 4 are included. DLSS is not part of this download (NVIDIA licensing):',
        'build from source to get it, see "DLSS" in README.md.',
        '',
        'Windows 11 "Bad Image ... 0xc0e90002": Smart App Control is blocking the unsigned DLLs.',
        '',
        'Not affiliated with Microsoft, Epic Games or The Coalition.'
    )
}

function Assert-NoGameFiles([string]$StageDir) {
    $bad = Get-ChildItem -Path $StageDir -Recurse -File | Where-Object {
        ($_.Name -ieq 'default.xex') -or ($_.Name -ilike 'Coalesced*.bin') -or
        ($_.Extension -in @('.xxx', '.tfc', '.bik', '.dds', '.upk', '.iso', '.xex'))
    }
    if ($bad) {
        foreach ($b in $bad) { Write-Host "Game file in staging: $($b.FullName)" -ForegroundColor Red }
        Fail 'Refusing to package: the staging folder would contain game data.'
    }
}

function Assert-NoNgx([string]$StageDir) {
    $withNgx = @(Find-NgxBinaries $StageDir)
    if (-not $withNgx.Count) {
        Write-Host 'No NVIDIA NGX names in the staged .dll and .exe files.'
        return
    }
    foreach ($b in $withNgx) { Write-Host "NVIDIA NGX in staging: $($b.Path) ($($b.Markers))" -ForegroundColor Red }
    Fail 'Refusing to package: DLSS must not ship. Build with scripts\build-sdk.ps1 -NoDlss.'
}

# ---- main ---------------------------------------------------------------------------------
$root = Split-Path -Parent $PSScriptRoot
$BuildDir = Resolve-RootedPath $root $BuildDir
$SdkDir = Resolve-RootedPath $root $SdkDir
$OutDir = Resolve-RootedPath $root $OutDir

$topName = 'gow3-recomp-{0}-win64' -f $Version
$stageDir = Join-Path (Join-Path $OutDir 'staging') $topName
$zipPath = Join-Path $OutDir ('{0}.zip' -f $topName)

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
# The staging folder is ours alone (out\package\staging\<name>); start it fresh.
if (Test-Path $stageDir) { Remove-Item -Recurse -Force $stageDir }
New-Item -ItemType Directory -Force -Path $stageDir | Out-Null

Step 'Binaries'
Copy-Binaries $BuildDir $SdkDir $stageDir
Step 'Visual C++ runtime'
Copy-VcRuntime $stageDir
Step 'Glyphs, settings, docs and QUICKSTART.txt'
Copy-Content $root $stageDir
Write-Quickstart $stageDir $Version
Step 'Game-data check'
Assert-NoGameFiles $stageDir
Step 'NVIDIA DLSS check'
Assert-NoNgx $stageDir

Step 'Zip'
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
# Windows' bsdtar writes standard zip paths ("/"); Compress-Archive in PowerShell 5 writes "\".
& (Join-Path $env:SystemRoot 'System32\tar.exe') -a -c -f $zipPath -C (Split-Path -Parent $stageDir) $topName
if ($LASTEXITCODE -ne 0) { Fail 'tar.exe could not create the zip.' }
$hash = (Get-FileHash -Path $zipPath -Algorithm SHA256).Hash
Set-Content -Path "$zipPath.sha256" -Encoding ASCII -Value ('{0}  {1}' -f $hash, (Split-Path -Leaf $zipPath))
Get-ChildItem -Path $stageDir -File | Sort-Object Name | Format-Table Name, Length -AutoSize
Write-Host ('Zip: {0} ({1:N1} MB)' -f $zipPath, ((Get-Item $zipPath).Length / 1MB))
Write-Host "SHA-256: $hash"
