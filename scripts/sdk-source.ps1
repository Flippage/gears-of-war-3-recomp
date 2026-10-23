# Dot-sourced by build-sdk.ps1 and build-android.ps1: a ReXGlue SDK checkout at the pinned commit
# with every sdk\patches\*.patch applied and the precompiled shaders in sdk\shaders replaced,
# prepared again whenever the commit, a patch or a replacement shader changes.
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

# Compiled D3D12 shaders (fxc output, .cso) that replace the SDK's bytecode headers of the same name
# (sdk\shaders\README.md says how they are made).
function Get-SdkShaderFiles([string]$patchDir) {
    $dir = Join-Path (Split-Path -Parent $patchDir) 'shaders\d3d12_5_1'
    if (-not (Test-Path $dir)) { return @() }
    return @(Get-ChildItem $dir -Filter *.cso | Sort-Object Name)
}

function Get-SdkSourceStamp([object[]]$patchFiles, [object[]]$shaderFiles = @()) {
    # foreach, not a pipeline: an empty list arrives as $null, which a pipeline would hash once.
    $hashes = foreach ($p in @($patchFiles) + @($shaderFiles)) { "$($p.Name):$(Get-FileSha256 $p.FullName)" }
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

# Writes each .cso as the bytecode header the SDK includes (const BYTE <name>[] = {...};).
function Install-SdkShaders([string]$sdk, [object[]]$shaderFiles) {
    $dest = Join-Path $sdk 'src\graphics\shaders\bytecode\d3d12_5_1'
    foreach ($f in $shaderFiles) {
        $name = $f.BaseName
        $header = Join-Path $dest "$name.h"
        if (-not (Test-Path $header)) { throw "No SDK shader $name to replace ($header)." }
        Write-Host "  replacing shader $name"
        $bytes = [IO.File]::ReadAllBytes($f.FullName)
        $sb = New-Object Text.StringBuilder
        [void]$sb.Append("// Replaced by the Gears of War 3 recomp from sdk/shaders/d3d12_5_1/$name.cso.`n")
        [void]$sb.Append("const BYTE $name[] = {`n")
        for ($i = 0; $i -lt $bytes.Length; $i += 32) {
            $end = [Math]::Min($i + 32, $bytes.Length) - 1
            [void]$sb.Append('    ').Append(($bytes[$i..$end] -join ', '))
            [void]$sb.Append($(if ($end -lt $bytes.Length - 1) { ",`n" } else { "`n" }))
        }
        [void]$sb.Append("};`n")
        [IO.File]::WriteAllText($header, $sb.ToString(), (New-Object Text.UTF8Encoding $false))
    }
}

# $true when it (re)prepared the tree, $false when $marker already records this commit and patches.
function Initialize-SdkSource([string]$sdk, [string]$marker, [string]$patchDir) {
    $patchFiles = Get-SdkPatchFiles $patchDir
    $shaderFiles = Get-SdkShaderFiles $patchDir
    $stamp = Get-SdkSourceStamp $patchFiles $shaderFiles
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
    Install-SdkShaders $sdk $shaderFiles
    Set-Content -Path $marker -Value $stamp -Encoding ascii
    return $true
}
