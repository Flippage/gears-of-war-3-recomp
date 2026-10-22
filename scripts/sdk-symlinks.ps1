# Dot-sourced by sdk-source.ps1 (build-sdk.ps1, build-android.ps1) and build-sdk-android.ps1.
#
# Without symlink privileges (no Developer Mode) git checks symlinks out as small text files that
# hold the target path; libmspack's cabextract/mspack/*.c are such links and fail to compile.
function Repair-SymlinkStubs([string]$repo) {
    $repos = @($repo) + @(git -C $repo submodule foreach --quiet --recursive 'echo $toplevel/$sm_path' 2>$null)
    foreach ($r in $repos) {
        $links = git -C $r ls-files -s 2>$null | Where-Object { $_ -match '^120000 ' } | ForEach-Object { ($_ -split "`t", 2)[1] }
        foreach ($rel in $links) { Copy-SymlinkTarget $r $rel }
    }
}

function Copy-SymlinkTarget([string]$repo, [string]$rel) {
    $link = Join-Path $repo $rel
    if (-not (Test-Path $link -PathType Leaf)) { return }
    if ((Get-Item $link).Attributes -band [IO.FileAttributes]::ReparsePoint) { return }
    # A rerun finds the links already replaced by the real source files.
    if ((Get-Item $link).Length -gt 260) { return }
    $target = (Get-Content $link -Raw).Trim()
    if ($target -match '[\r\n<>|"*?]') { return }
    $resolved = Join-Path (Split-Path $link -Parent) $target
    if (Test-Path $resolved -PathType Leaf) {
        Copy-Item $resolved $link -Force
        Write-Host "  materialized symlink $rel -> $target"
    }
}
