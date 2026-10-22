# Known issues

## Black blotches on faces in in-engine cutscenes (open)

**Symptom.** In in-engine cutscenes, close-up faces (Marcus, Anya, Cole...) get hard-edged black
patches on the brows, around the eyes, cheeks, mouth, jaw and neck: the parts that deform while a
character talks. They appear only while the cutscene plays and vanish as soon as the game is
paused. Gameplay faces look fine.

**Xenia renders it correctly.** Xenia canary (netplay build, Oct 2026, D3D12, RTV) shows the same
scene of the **same base executable (`0.0.0.11`)** with clean faces, with or without the community
patches and with or without FSR/FXAA. So the cause is in this project's emulation: the recompiled
CPU code (ReXGlue codegen) or the GPU runtime (ReXGlue's Xenia-derived Xenos renderer plus
`sdk\patches`).

### Ruled out (each tested, blotches unchanged)

| Tried | How |
|---|---|
| MSAA tiling | `gow3_black_shading_fix` (GUseTilingMode = FALSE) on and off |
| Ambient occlusion, bloom, DoF, motion blur, lens flares | community patches |
| Shadow patches | Performance Option B on; `gow3_disable_dynamic_shadows = true` (live) |
| Render target path | ROV (default on NVIDIA here) and RTV |
| Scalar ALU swizzle with three-source vector ops | `sdk/patches/0030` (Xenia `92ada8ebc0`) |
| Signed memexport rounding bias | `sdk/patches/0031` (Xenia `da47dfaacf`) |
| Invalid fetch constants returning black | `gpu_allow_invalid_fetch_constants = true` |
| Memexport readback | `readback_memexport` on and off |
| The full-screen shadow projection pass (`ps 857CECDEB64A657F`) | skipped with `debug_skip_ps`: shadows went, blotches stayed |
| Every pixel shader that runs only while the shot plays (two halves) | skipped live with `debug_skip_ps.txt` |
| Unsigned-biased integer texture scaling (Xenia `2ddc5ef737`) | not applicable: the SDK lacks that feature |
| Texture streaming pool size | `gow3_texture_pool_mb` 145 / 196 |
| Shader cache staleness | caches cleared and rebuilt |

### What the frame traces show

Captured with the `-Dev` runtime (`gow2_trace.now`) during the Act 1 "Troubled Past" opening
cutscene (Marcus close-up):

- While playing, a frame draws ~1,140 times; paused, ~575 with a partly different set of shaders.
- In a playing frame the game resolves to the same address, `0FCD8000`, eight times, alternating
  per light between shadow depth (format 22, 1120x672 / 1120x544 / 544x288) and the 1280x720
  shadow mask (format 6) that every lit pass then samples as `tf0`. The paused frame resolves a
  shadow depth there too and is clean, so this aliasing alone is not proven to be the cause.
- No single pixel shader in the shot is responsible (skipping them did not remove the blotches),
  which points at the data a draw reads (vertex data from the CPU, or a texture/resolve) rather
  than at a shader translation.

### Ideas not yet tried

1. Compare the face draw's vertex data and constants between this build and Xenia. That needs a
   frame trace from both: Xenia's GPU > Trace Frame writes `scratch\gpu\*.xtr` (the folder must
   exist), but the ReXGlue runtime has no trace writer yet. Different data = CPU-side (recompiled
   VMX128 math, e.g. facial animation); same data = GPU runtime.
2. Review the recompiler's VMX128 implementations used by the animation code against Xenia's
   (`vpkd3d128`/`vupkd3d128` FLOAT16 and NORMSHORT paths looked correct on a first read).
3. Report to the ReXGlue SDK and the Gears 2 port, which share this runtime.
