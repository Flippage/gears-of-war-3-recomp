# Replacement SDK shaders

The ReXGlue SDK ships its GPU compute shaders precompiled (`src/graphics/shaders/bytecode/`). A few
of them need changes that a text patch cannot carry well (a compiled shader is a large byte array),
so the changed shaders are kept here as fxc output, and `scripts\build-sdk.ps1` writes each
`d3d12_5_1\<name>.cso` over the SDK's `bytecode\d3d12_5_1\<name>.h` after applying `sdk\patches`.
Changing a file here makes the next `build-sdk.ps1` prepare the SDK again.

| Shaders | Why |
|---|---|
| `resolve_fast_*_scaled_cs`, `resolve_full_*_scaled_cs` (9) | `0055-edram-scaled-guest-samples.patch`: with resolution scaling, the resolve shaders read MSAA samples arranged per guest pixel (`XeEdramOffsetIntsGuestSamples`) when bit 30 of the resolve EDRAM info is set, matching the ROV pixel shaders, so 1x and 4x views of the same memory line up at 2x/3x. Unscaled shaders and the clear shaders are unchanged. (The 2x speckle itself was fixed by 0058/0059; see docs/KNOWN_ISSUES.md.) |

## Rebuilding

The SDK's resolve and clear shaders are Xenia's (BSD-3-Clause) as of commit `28b69c2^`: compiling
those sources with fxc's default flags reproduces all 22 of them byte for byte.
`apply-guest-samples.py` makes the changes, and `build-resolve-shaders.ps1` does everything:

```
powershell -ExecutionPolicy Bypass -File sdk\shaders\build-resolve-shaders.ps1 -Xenia <xenia-canary clone> -Check
```

`-Check` first compiles the unmodified sources and compares them with the SDK's bytecode (every line
should say `same`). Needs Python and fxc from the Windows SDK. Every change in
`apply-guest-samples.py` is inside `#ifdef XE_RESOLVE_RESOLUTION_SCALED` and behind the runtime bit,
so the unscaled shaders compile to the same bytes as before.
