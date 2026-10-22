# Dot-sourced by build-sdk.ps1 and package.ps1. NVIDIA's DLSS SDK license keeps its NGX library and
# the DLSS runtime out of an open-source release (THIRD_PARTY_NOTICES.md, "NVIDIA DLSS"); these
# functions find either one in a built binary.

# Names the NGX static library (NVSDK_NGX_* functions, nvsdk_ngx.h) and the DLSS runtime
# (nvngx_dlss.dll) leave in a binary. Matched without regard to case.
$NgxMarkers = @('nvsdk_ngx', 'nvngx')

function Get-BinaryTexts([byte[]]$bytes) {
    # UTF-16 strings can start at an odd offset, so both alignments are decoded.
    $texts = @([Text.Encoding]::ASCII.GetString($bytes), [Text.Encoding]::Unicode.GetString($bytes))
    if ($bytes.Length -gt 1) { $texts += [Text.Encoding]::Unicode.GetString($bytes, 1, $bytes.Length - 1) }
    return $texts
}

function Test-TextsContain([string[]]$texts, [string]$marker) {
    foreach ($text in $texts) {
        if ($text.IndexOf($marker, [StringComparison]::OrdinalIgnoreCase) -ge 0) { return $true }
    }
    return $false
}

function Find-NgxMarkers([string]$path) {
    $texts = Get-BinaryTexts ([IO.File]::ReadAllBytes($path))
    return @($NgxMarkers | Where-Object { Test-TextsContain $texts $_ })
}

# Every .dll and .exe under $dir that holds an NGX marker, with the markers it holds.
function Find-NgxBinaries([string]$dir) {
    $binaries = Get-ChildItem -Path $dir -Recurse -File -Include '*.dll', '*.exe' -ErrorAction SilentlyContinue
    foreach ($binary in $binaries) {
        $markers = Find-NgxMarkers $binary.FullName
        if ($markers.Count) { [pscustomobject]@{ Path = $binary.FullName; Markers = $markers -join ', ' } }
    }
}
