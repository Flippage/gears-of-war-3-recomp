# Starts two Gears of War 3 instances on this PC, windowed, linked by System Link over loopback, for
# local co-op tests (adapted from the Gears of War 2 port's scripts/coop-instances.ps1, BSD-3-Clause).
#
#   powershell -ExecutionPolicy Bypass -File scripts\coop-local.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\coop-local.ps1 -Stop
#
# Each instance gets its own user data folder under -OutDir (its own profile, saves and System Link
# XUID), its own log file and its own loopback address (127.0.0.1 and 127.0.0.2); System Link
# broadcasts go to the other one as unicast copies (net_broadcast_targets).
#
# Input: player 1 uses the controller (SDL). Player 2 uses the keyboard (--mnk_mode) and the XInput
# backend, which does not see a DualSense, so the controller does not drive both. A window only gets
# input while it has the focus: click the one you want to control. -P2Controller gives player 2 the
# controller too (useful with two pads; with one, both react while focused).
#
# The upscaler is off in these windows (two instances share the GPU); your gow3.toml is not changed.
# Each instance has its own shader cache, copied from %LOCALAPPDATA%\GearsOfWar3PC\cache the first time.
# -SeedUser <folder> copies a user data folder (for example your own saves) into each new instance
# folder the first time; the source is only read.
param(
    [string]$Exe = '',
    [string]$OutDir = '',
    [int]$Width = 1280,
    [int]$Height = 720,
    [string]$SeedUser = '',
    [switch]$P2Controller,
    [switch]$Stop
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Exe) { $Exe = Join-Path $root 'out\build\release\gow3.exe' }
if (-not $OutDir) { $OutDir = Join-Path $root 'out\coop-local' }
$pidFile = Join-Path $OutDir 'pids.txt'

if ($Stop) {
    if (Test-Path $pidFile) {
        foreach ($id in Get-Content $pidFile) {
            Get-Process -Id $id -ErrorAction SilentlyContinue | Stop-Process -ErrorAction SilentlyContinue
        }
        Remove-Item $pidFile -ErrorAction SilentlyContinue
    }
    Write-Host 'Co-op instances stopped.'
    return
}

if (-not (Test-Path $Exe)) { throw "gow3.exe not found: $Exe (build it first, or pass -Exe)" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$targets = '127.0.0.1,127.0.0.2'
$ids = @()
foreach ($i in 1, 2) {
    $dir = Join-Path $OutDir "p$i"
    $user = Join-Path $dir 'user'
    $cache = Join-Path $dir 'cache'
    if (-not (Test-Path $user)) {
        New-Item -ItemType Directory -Force $dir | Out-Null
        if ($SeedUser) { Copy-Item -Recurse -Path $SeedUser -Destination $user }
        else { New-Item -ItemType Directory -Force $user | Out-Null }
    }
    # Each instance keeps its own shader cache (two processes must not write one pipeline library);
    # the first time, it starts from a copy of your main cache so the pipelines need not be rebuilt.
    if (-not (Test-Path $cache)) {
        $mainCache = Join-Path $env:LOCALAPPDATA 'GearsOfWar3PC\cache'
        if (Test-Path $mainCache) { Copy-Item -Recurse -Path $mainCache -Destination $cache }
        else { New-Item -ItemType Directory -Force $cache | Out-Null }
    }
    $gameArgs = @(
        "--user_data_root=`"$user`"", "--cache_root=`"$cache`"",
        "--log_file=`"$(Join-Path $dir 'game.log')`"",
        '--fullscreen=false', "--window_width=$Width", "--window_height=$Height",
        '--net_link_active=true', "--net_bind_ip=127.0.0.$i", "--net_broadcast_targets=$targets",
        "--user_name=Player$i", '--system_link_trace=true',
        '--gow2_temporal=false', '--gow2_upscaler=off'
    )
    if ($i -eq 2 -and -not $P2Controller) { $gameArgs += '--input_backend=xinput', '--mnk_mode=true' }
    $p = Start-Process -FilePath $Exe -ArgumentList $gameArgs -WorkingDirectory (Split-Path $Exe) -PassThru
    $ids += $p.Id
    Write-Host "Player $i started (pid $($p.Id)), data in $dir"
    if ($i -eq 1) { Start-Sleep -Seconds 8 }  # let the first one take the shader cache before the second
}
Set-Content -Path $pidFile -Value $ids
Write-Host ''
Write-Host 'Player 1: controller. Player 2: keyboard (Enter = Start, Space = A, WASD = move, arrows = look).'
Write-Host 'Click a window to give it the input. Stop both with: scripts\coop-local.ps1 -Stop'
