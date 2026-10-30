<#
.SYNOPSIS
  Packages an already-built gow3 tree into a Windows release zip (no game files, no DLSS).

.DESCRIPTION
  Copies the base-game build (builds\gow3-base.exe, and gow3.exe as a copy of it), the Title
  Update 6 build (builds\gow3-tu6.exe), launcher.exe, version.txt, the release runtime DLLs
  (scripts\build-sdk.ps1 -NoDlss), AMD FidelityFX,
  the Visual C++ runtime, the button prompt glyphs, config\gow3.default.toml (no gow3.toml: the
  first start makes it), the docs and a generated
  QUICKSTART.txt into a staging folder, and compresses it to
  <OutDir>\gow3-recomp-<Version>-win64.zip with a .sha256 file next to it.

  The zip never contains game data: packaging stops if default.xex, any *.xex / *.xexp / *.xxx /
  *.tfc / *.bik / *.dds file, Coalesced*.bin, a path with GearGame, or a file starting with an
  STFS magic (CON, LIVE, PIRS) would be staged. It never contains NVIDIA DLSS either: packaging
  stops if a nvngx_dlss*.dll is staged or a staged .dll or .exe holds an NGX name
  (scripts\ngx-scan.ps1). DLSS stays a
  build-from-source feature (README, "DLSS").

  Adapted from the Gears of War 2 port's scripts\package.ps1 (BSD-3-Clause, Santiago Quiroz
  Upegui and the gow2-recomp contributors).

.PARAMETER Version
  Release version, e.g. "0.1.0". Used in the zip and top folder name.

.PARAMETER BuildDir
  Folder with the built base-game gow3.exe and launcher.exe. Default: out\build\release.
  (-FromBuild is an alias.)

.PARAMETER FromBuildTu6
  Folder with the Title Update 6 build of gow3.exe. Default: out\build\tu6.

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

    [Alias('FromBuild')]
    [string]$BuildDir = 'out\build\release',

    [string]$FromBuildTu6 = 'out\build\tu6',

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

function Assert-Unlocked([string]$Path) {
    try {
        # Opening for write (no truncation) fails with a sharing violation while the exe runs.
        $fs = [System.IO.File]::Open($Path, 'Open', 'Write', 'ReadWrite')
        $fs.Dispose()
    } catch {
        Fail "$Path is locked or unreadable (is the game or launcher running?): $($_.Exception.Message)"
    }
}

function Find-Llvm([string]$Tool) {
    $path = Join-Path (Join-Path $env:ProgramFiles 'LLVM\bin') $Tool
    if (-not (Test-Path $path)) { Fail "$Tool not found at $path (install LLVM, as scripts\build.ps1 needs)." }
    return $path
}

# llvm-readobj's output lines; a failed run stops packaging (the export check must not pass by
# reading nothing).
function Invoke-ReadObj([string]$Option, [string]$Binary) {
    $lines = @(& (Find-Llvm 'llvm-readobj.exe') $Option $Binary)
    if ($LASTEXITCODE -ne 0) { Fail "llvm-readobj $Option $Binary failed (exit code $LASTEXITCODE)." }
    return ,$lines
}

# Symbols a binary imports from one DLL (llvm-readobj --coff-imports).
function Get-ImportedSymbols([string]$Binary, [string]$DllName) {
    $in = $false
    foreach ($line in (Invoke-ReadObj '--coff-imports' $Binary)) {
        if ($line -match '^Import \{') { $in = $false; continue }
        if ($line -match '^  Name: (.+)$') { $in = ($Matches[1].Trim() -ieq $DllName); continue }
        if ($in -and $line -match '^  Symbol: (.+) \(\d+\)$') { $Matches[1] }
    }
}

function Get-ExportedSymbols([string]$Dll) {
    $set = New-Object 'System.Collections.Generic.HashSet[string]'
    foreach ($line in (Invoke-ReadObj '--coff-exports' $Dll)) {
        if ($line -match '^  Name: (.+)$') { [void]$set.Add($Matches[1]) }
    }
    return ,$set
}

# Every function the exes import from the runtime DLLs must exist in the DLLs that ship
# (the build folders hold DLSS-enabled copies, so their hashes say nothing about the shipped ones).
function Assert-RuntimeExports([string[]]$Exes, [string]$FromSdk) {
    foreach ($dllName in @('rexruntime.dll', 'rexgpu-xenos.dll')) {
        $dll = Join-Path $FromSdk $dllName
        if (-not (Test-Path $dll)) { continue }
        $exports = Get-ExportedSymbols $dll
        if (-not $exports.Count) { Fail "llvm-readobj found no exports in $dll." }
        foreach ($exe in $Exes) {
            $imports = @(Get-ImportedSymbols $exe $dllName)
            # Every exe links rexruntime.dll (the launcher for ImGui): none read means a broken check.
            if ($dllName -ieq 'rexruntime.dll' -and -not $imports.Count) {
                Fail "llvm-readobj found no imports from rexruntime.dll in $exe."
            }
            $missing = @($imports | Where-Object { -not $exports.Contains($_) })
            if ($missing.Count) {
                foreach ($m in $missing) { Write-Host "Missing in ${dllName}: $m" -ForegroundColor Red }
                Fail "$exe imports $($missing.Count) symbol(s) that the shipped $dllName does not export."
            }
        }
    }
    Write-Host 'Every runtime import of the staged exes is exported by the shipped runtime DLLs.'
}

function Get-RuntimeHash([string]$Dir) {
    $dll = Join-Path $Dir 'rexruntime.dll'
    if (-not (Test-Path $dll)) { Fail "rexruntime.dll not found in $Dir." }
    return (Get-FileHash -Path $dll -Algorithm SHA256).Hash
}

function Copy-Binaries([string]$FromBuild, [string]$FromBuildTu6, [string]$FromSdk, [string]$StageDir, [string]$Version) {
    $exe = Join-Path $FromBuild 'gow3.exe'
    $exeTu6 = Join-Path $FromBuildTu6 'gow3.exe'
    $launcher = Join-Path $FromBuild 'launcher.exe'
    if (-not (Test-Path $exe)) { Fail "gow3.exe not found in $FromBuild. Run scripts\build.ps1 first." }
    if (-not (Test-Path $exeTu6)) { Fail "gow3.exe not found in $FromBuildTu6. Run scripts\build.ps1 -Game tu6 first." }
    if (-not (Test-Path $launcher)) { Fail "launcher.exe not found in $FromBuild. Build the launcher target." }
    foreach ($f in @($exe, $exeTu6, $launcher)) { Assert-Unlocked $f }
    # Both builds load the one rexruntime.dll in the zip; built against different SDK builds they could not share it.
    if ((Get-RuntimeHash $FromBuild) -ne (Get-RuntimeHash $FromBuildTu6)) {
        Fail 'The base and TU6 builds were made against different SDK builds (their rexruntime.dll files differ).'
    }
    Assert-RuntimeExports @($exe, $exeTu6, $launcher) $FromSdk
    $builds = Join-Path $StageDir 'builds'
    New-Item -ItemType Directory -Force -Path $builds | Out-Null
    Copy-Item -Force $exe (Join-Path $builds 'gow3-base.exe')
    Copy-Item -Force $exeTu6 (Join-Path $builds 'gow3-tu6.exe')
    Copy-Item -Force $exe (Join-Path $StageDir 'gow3.exe')
    Copy-Item -Force $launcher $StageDir
    Set-Content -Path (Join-Path $StageDir 'version.txt') -Encoding ASCII -Value $Version
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
    # No gow3.toml in the release: gow3.exe and launcher.exe make it from config\gow3.default.toml on
    # the first start, so extracting a new zip over an install never replaces the player's settings.
    $config = Join-Path $StageDir 'config'
    New-Item -ItemType Directory -Force -Path $config | Out-Null
    Copy-Item -Force (Join-Path $Root 'config\gow3.toml') (Join-Path $config 'gow3.default.toml')
    $glyphs = Join-Path $StageDir 'assets\glyphs'
    New-Item -ItemType Directory -Force -Path $glyphs | Out-Null
    Copy-Item -Recurse -Force (Join-Path $Root 'assets\glyphs\*') $glyphs
    $fonts = Join-Path $StageDir 'assets\fonts'
    New-Item -ItemType Directory -Force -Path $fonts | Out-Null
    Copy-Item -Recurse -Force (Join-Path $Root 'assets\fonts\*') $fonts
    $licenses = Join-Path $StageDir 'licenses'
    New-Item -ItemType Directory -Force -Path $licenses | Out-Null
    Copy-Item -Force (Join-Path $Root 'thirdparty\imgui-backends\LICENSE.txt') (Join-Path $licenses 'Dear-ImGui-MIT.txt')
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
        'Bring your own copy of Gears of War 3: the ISO of your own disc, or the game extracted to a',
        'folder that contains default.xex and the GearGame folder (for example with xdvdfs:',
        '  https://github.com/antangelo/xdvdfs ).',
        '',
        'FIRST RUN',
        'Run gow3.exe. It looks for your ISO or game folder (in this folder and the folders around it)',
        'and walks you through setup. If it finds an ISO it extracts the game to a folder you choose;',
        'if it finds nothing it asks you to pick your ISO or game folder.',
        'For Title Update 6 and DLC, put your own packages next to the game (or this folder); setup',
        'offers them to you, and you choose whether to install them. Unpack any .zip, .7z or .rar',
        'first: the launcher reads the package files in folders, not archives.',
        'Your ISO and your packages are never changed. Setup writes only to the game folder it',
        'extracts, its TU6 and DLC subfolders, and its own files next to gow3.exe.',
        '',
        'LAUNCHER',
        'launcher.exe opens the settings, lets you upgrade to or switch between the base-game build',
        'and the Title Update 6 build, repairs the setup, and checks for new releases (you choose',
        'Update, Skip or Later). Settings can still be changed in the game with F10.',
        'The first launch of the game compiles shaders and can show "Not responding" for a few minutes.',
        'Let it finish.',
        'Advanced: you can still write the game folder path on the first line of gow3_game_path.txt',
        'next to gow3.exe, or run gow3.exe --game_data_root="C:\path\to\game" (the launcher uses',
        'the same folder when gow3.exe hands over to it).',
        '',
        'UPDATING',
        'Prefer the launcher''s Update: it keeps your settings, game folder and build. To update by',
        'hand, extract the new zip over this folder and replace everything (the zip has no',
        'gow3.toml, so your settings are kept), then run launcher.exe once: it restores your Title',
        'Update 6 build (the zip''s gow3.exe is the base-game build).',
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
        'WARNING: the Title Update 6 build reads base-game saves, but the base-game build cannot read',
        'saves the Title Update 6 build wrote. Once you have played with Title Update 6, keep using',
        'that build. If the base-game build reports "Error Loading Checkpoints", choose Cancel, not',
        'Overwrite, or the updated saves are deleted.',
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

function Test-StfsMagic([string]$Path, [string[]]$Magics) {
    $buf = New-Object byte[] 4
    $fs = [System.IO.File]::OpenRead($Path)
    try { $n = $fs.Read($buf, 0, 4) } finally { $fs.Dispose() }
    if ($n -lt 4) { return $false }
    return ($Magics -ccontains [System.Text.Encoding]::ASCII.GetString($buf))
}

function Assert-NoGameFiles([string]$StageDir) {
    $stfs = @('CON ', 'LIVE', 'PIRS')
    $bad = Get-ChildItem -Path $StageDir -Recurse -File -Force | Where-Object {
        ($_.Name -ieq 'default.xex') -or ($_.Name -ilike 'Coalesced*.bin') -or
        ($_.FullName.Substring($StageDir.Length) -ilike '*GearGame*') -or
        ($_.Extension -in @('.xxx', '.tfc', '.bik', '.dds', '.upk', '.iso', '.xex', '.xexp')) -or
        (Test-StfsMagic $_.FullName $stfs)
    }
    if ($bad) {
        foreach ($b in $bad) { Write-Host "Game file in staging: $($b.FullName)" -ForegroundColor Red }
        Fail 'Refusing to package: the staging folder would contain game data.'
    }
}

function Assert-NoNgx([string]$StageDir) {
    $dlss = @(Get-ChildItem -Path $StageDir -Recurse -File -Force | Where-Object { $_.Name -ilike 'nvngx_dlss*.dll' })
    if ($dlss.Count) {
        foreach ($d in $dlss) { Write-Host "DLSS in staging: $($d.FullName)" -ForegroundColor Red }
        Fail 'Refusing to package: DLSS must not ship. Build with scripts\build-sdk.ps1 -NoDlss.'
    }
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
$FromBuildTu6 = Resolve-RootedPath $root $FromBuildTu6
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
Copy-Binaries $BuildDir $FromBuildTu6 $SdkDir $stageDir $Version
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
