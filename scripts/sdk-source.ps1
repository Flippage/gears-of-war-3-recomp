# Dot-sourced by build-sdk.ps1 and build-android.ps1: a ReXGlue SDK checkout at the pinned commit
# with every sdk\patches\*.patch applied, prepared again whenever the commit or a patch changes.
# Failures throw; the calling script turns them into its own error message.

. (Join-Path $PSScriptRoot 'sdk-symlinks.ps1')

# The SDK commit the project and the patches are made for (must match the prebuilt SDK the
# Windows game is compiled against).
$SdkUrl = 'https://github.com/rexglue/rexglue-sdk.git'
$SdkCommit = '923c1a5'

function Get-SdkPatchFiles([string]$patchDir) {
    if (-not (Test-Path $patchDir)) { return @() }
    return @(Get-ChildItem $patchDir -Filter *.patch | Sort-Object Name)
}

# .NET directly: Get-FileHash lives in a module that a Windows PowerShell started from pwsh may
# fail to load (pwsh's PSModulePath is inherited).
function Get-FileSha256([string]$path) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($path))).Replace('-', '') }
    finally { $sha.Dispose() }
}

function Get-SdkSourceStamp([object[]]$patchFiles) {
    # foreach, not a pipeline: an empty list arrives as $null, which a pipeline would hash once.
    $hashes = foreach ($p in $patchFiles) { "$($p.Name):$(Get-FileSha256 $p.FullName)" }
    return "$SdkCommit|$($hashes -join ',')"
}

function Test-SdkSourceCurrent([string]$marker, [string]$stamp) {
    return (Test-Path $marker) -and ((Get-Content $marker -Raw).Trim() -eq $stamp)
}

# Windows PowerShell 5.1 turns a native command's stderr into errors under 'Stop', and git reports
# progress there; the exit code decides instead.
function Invoke-SdkGit([string]$what, [string[]]$arguments) {
    $ErrorActionPreference = 'Continue'
    & git @arguments 2>&1 | ForEach-Object { Write-Host "  $_" }
    if ($LASTEXITCODE -ne 0) { throw "$what failed (exit code $LASTEXITCODE)." }
}

function Reset-SdkSource([string]$sdk) {
    Invoke-SdkGit 'git checkout' @('-C', $sdk, 'checkout', '-f', $SdkCommit)
    Invoke-SdkGit 'git clean' @('-C', $sdk, 'clean', '-fdq', '-e', 'out')
    Invoke-SdkGit 'git submodule update' @('-C', $sdk, 'submodule', 'update', '--init', '--recursive', '--depth', '1', '--jobs', '8')
    Repair-SymlinkStubs $sdk
}

function Install-SdkPatches([string]$sdk, [object[]]$patchFiles) {
    foreach ($p in $patchFiles) {
        Write-Host "  applying $($p.Name)"
        Invoke-SdkGit "applying patch $($p.Name)" @('-C', $sdk, 'apply', '--ignore-whitespace', '--whitespace=nowarn', $p.FullName)
    }
}

# $true when it (re)prepared the tree, $false when $marker already records this commit and patches.
function Initialize-SdkSource([string]$sdk, [string]$marker, [string]$patchDir) {
    $patchFiles = Get-SdkPatchFiles $patchDir
    $stamp = Get-SdkSourceStamp $patchFiles
    if (-not (Test-Path (Join-Path $sdk '.git'))) {
        Write-Host "Cloning the ReXGlue SDK ($SdkCommit) into $sdk"
        Invoke-SdkGit 'git clone' @('clone', '--filter=blob:none', $SdkUrl, $sdk)
    }
    if (Test-SdkSourceCurrent $marker $stamp) { return $false }
    Write-Host "Preparing $sdk ($SdkCommit + $($patchFiles.Count) patches)"
    # A half-prepared tree must not pass for a prepared one.
    if (Test-Path $marker) { Remove-Item $marker -Force }
    Reset-SdkSource $sdk
    Install-SdkPatches $sdk $patchFiles
    Set-Content -Path $marker -Value $stamp -Encoding ascii
    return $true
}
