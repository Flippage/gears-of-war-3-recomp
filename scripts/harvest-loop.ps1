<#
.SYNOPSIS
  Boot, read the "unregistered function" FATAL, add it to hints\runtime.toml, rebuild, repeat.
  Same idea as santiquiroz/gears-of-war-2-pc-port scripts\run-harvest-loop.sh.

.PARAMETER MaxRuns
  Stop after this many boots.
.PARAMETER Seconds
  How long one boot may run; a boot still alive at the end counts as "got further" and stops the loop.
#>
param(
    [int]$MaxRuns = 15,
    [int]$Seconds = 90,
    [string]$GameData = 'C:\Users\flipp\GearsRecomps\Gears3-GameFiles'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$out = Join-Path $root 'out\build\release'
$hints = Join-Path $root 'hints\runtime.toml'
$history = Join-Path $root 'out\logs\harvest-loop.txt'
New-Item -ItemType Directory -Force (Split-Path $history) | Out-Null

function Log($msg) { $line = "[$(Get-Date -Format HH:mm:ss)] $msg"; Write-Host $line; Add-Content $history $line }

for ($run = 1; $run -le $MaxRuns; $run++) {
    $log = Join-Path $out 'run.log'
    Remove-Item $log -ErrorAction SilentlyContinue
    $p = Start-Process (Join-Path $out 'gow3.exe') -WorkingDirectory $out -PassThru `
        -ArgumentList "--game_data_root=$GameData", '--fullscreen=false', '--log_file=run.log'
    $t = 0
    while (-not $p.HasExited -and $t -lt $Seconds) { Start-Sleep 1; $t++ }
    if (-not $p.HasExited) {
        Stop-Process -Id $p.Id -Force
        Log "run ${run}: still running after ${Seconds}s - stopping the loop"
        Copy-Item $log (Join-Path $root "out\logs\run-$run.log") -Force
        exit 0
    }
    $code = '0x{0:X8}' -f $p.ExitCode
    Copy-Item $log (Join-Path $root "out\logs\run-$run.log") -Force
    $fatal = Select-String $log -Pattern 'unregistered function at guest address (0x[0-9A-Fa-f]{8})' | Select-Object -Last 1
    if (-not $fatal) {
        Log "run ${run}: exited after ${t}s with $code and no unregistered-function FATAL - needs a look (out\logs\run-$run.log)"
        exit 1
    }
    $addr = $fatal.Matches[0].Groups[1].Value.ToUpper().Replace('0X', '0x')
    if (Select-String $hints -Pattern "^$addr " -Quiet) {
        Log "run ${run}: $addr is already a hint but still unregistered - needs a look"
        exit 1
    }
    $n = (Select-String $hints -Pattern '^0x').Count + 1
    Add-Content $hints "$addr = {}  # boot run $n"
    Log "run ${run}: ${t}s, $code, added $addr; rebuilding"
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build.ps1') *>&1 |
        Out-File (Join-Path $root 'out\logs\harvest-build.txt') -Encoding utf8
    if ($LASTEXITCODE -ne 0) { Log "build failed after adding $addr (out\logs\harvest-build.txt)"; exit 1 }
}
Log "stopped after $MaxRuns runs"
