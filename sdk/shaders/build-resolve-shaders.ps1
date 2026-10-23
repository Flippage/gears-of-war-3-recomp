# Rebuilds sdk\shaders\d3d12_5_1\*.cso: the ReXGlue SDK's resolution-scaled resolve shaders with the
# Gears of War 3 guest-sample EDRAM layout (sdk\patches\0055-edram-scaled-guest-samples.patch).
#
#   powershell -ExecutionPolicy Bypass -File sdk\shaders\build-resolve-shaders.ps1 -Xenia <xenia-canary clone>
#
# The SDK ships these shaders precompiled from Xenia's sources as of $XeniaCommit (fxc with its default
# flags reproduces every resolve and clear shader byte for byte). This script exports those sources
# from a xenia-canary clone (it needs that commit; a clone made with --filter=blob:none fetches the
# files on demand), applies apply-guest-samples.py, and compiles the changed shaders with fxc from the
# Windows SDK. Run -Check to also compile the unchanged shaders and compare them with the SDK's.
param(
    [Parameter(Mandatory = $true)][string]$Xenia,
    [string]$Sdk = '',
    [switch]$Check
)
$ErrorActionPreference = 'Stop'
$XeniaCommit = '28b69c2^'
$here = $PSScriptRoot
$root = Split-Path -Parent (Split-Path -Parent $here)
if (-not $Sdk) { $Sdk = Join-Path $root 'out\sdk-build\rexglue-sdk' }

$fxc = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\fxc.exe" -ErrorAction SilentlyContinue |
    Sort-Object FullName | Select-Object -Last 1
if (-not $fxc) { throw 'fxc.exe not found (install the Windows SDK).' }

$work = Join-Path ([IO.Path]::GetTempPath()) "gow3-resolve-shaders-$PID"
New-Item -ItemType Directory -Force $work | Out-Null
try {
    $tar = Join-Path $work 'src.tar'
    & git -C $Xenia archive -o $tar $XeniaCommit src/xenia/gpu/shaders src/xenia/ui/shaders
    if ($LASTEXITCODE -ne 0) { throw "git archive $XeniaCommit failed." }
    # Windows' own tar: a Git Bash tar on PATH reads "C:" as a remote host.
    & (Join-Path $env:SystemRoot 'System32\tar.exe') -xf $tar -C $work
    if ($LASTEXITCODE -ne 0) { throw 'tar failed.' }
    $shaders = Join-Path $work 'src\xenia\gpu\shaders'

    if ($Check) {
        # The unmodified sources must reproduce the SDK's bytecode exactly.
        Write-Host 'Checking the unmodified sources against the SDK bytecode...'
        $ref = Join-Path $Sdk 'src\graphics\shaders\bytecode\d3d12_5_1'
        foreach ($src in Get-ChildItem $shaders -Filter 'resolve_*.cs.xesl') {
            $name = $src.Name -replace '\.cs\.xesl$', '_cs'
            if (-not (Test-Path (Join-Path $ref "$name.h"))) { continue }
            $out = Join-Path $work "$name.orig.h"
            & $fxc.FullName /nologo /D SHADING_LANGUAGE_HLSL_XE=1 /I $shaders /Fh $out /T cs_5_1 /Vn $name $src.FullName | Out-Null
            if ($LASTEXITCODE -ne 0) { throw "fxc failed on $($src.Name)." }
            $a = [regex]::Matches(([regex]::Match((Get-Content $out -Raw), '\[\]\s*=\s*\{(.*?)\}', 'Singleline').Groups[1].Value), '0x[0-9a-fA-F]+|\d+') | ForEach-Object { [Convert]::ToInt32($_.Value, $(if ($_.Value.StartsWith('0x')) { 16 } else { 10 })) }
            $b = [regex]::Matches(([regex]::Match((Get-Content (Join-Path $ref "$name.h") -Raw), '\[\]\s*=\s*\{(.*?)\}', 'Singleline').Groups[1].Value), '0x[0-9a-fA-F]+|\d+') | ForEach-Object { [Convert]::ToInt32($_.Value, $(if ($_.Value.StartsWith('0x')) { 16 } else { 10 })) }
            $same = ($a.Count -eq $b.Count) -and -not (Compare-Object $a $b -SyncWindow 0)
            Write-Host ("  {0}: {1}" -f $name, $(if ($same) { 'same' } else { 'DIFFERENT' }))
        }
    }

    & python (Join-Path $here 'apply-guest-samples.py') $shaders
    if ($LASTEXITCODE -ne 0) { throw 'apply-guest-samples.py failed.' }

    $dest = Join-Path $here 'd3d12_5_1'
    New-Item -ItemType Directory -Force $dest | Out-Null
    foreach ($name in @(
            'resolve_fast_32bpp_1x2xmsaa_scaled_cs', 'resolve_fast_32bpp_4xmsaa_scaled_cs',
            'resolve_fast_64bpp_1x2xmsaa_scaled_cs', 'resolve_fast_64bpp_4xmsaa_scaled_cs',
            'resolve_full_8bpp_scaled_cs', 'resolve_full_16bpp_scaled_cs', 'resolve_full_32bpp_scaled_cs',
            'resolve_full_64bpp_scaled_cs', 'resolve_full_128bpp_scaled_cs')) {
        $src = Join-Path $shaders (($name -replace '_cs$', '') + '.cs.xesl')
        Write-Host "  $name"
        & $fxc.FullName /nologo /D SHADING_LANGUAGE_HLSL_XE=1 /I $shaders /Fo (Join-Path $dest "$name.cso") /T cs_5_1 $src | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "fxc failed on $name." }
    }
    Write-Host "Done: $dest (scripts\build-sdk.ps1 puts them into the SDK)."
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
