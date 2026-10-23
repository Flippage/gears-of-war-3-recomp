# Known issues

## Black blotches on faces in in-engine cutscenes (fixed)

**Cause and fix (2026-10-23, `sdk/patches/0054`).** The runtime skipped requesting a vertex buffer
when its fetch constant kept the same address and size, even after the swap had cleared the buffer's
in-sync bit. Gears 3 rewrites each face's morph buffer in place every frame, so the face's depth-only
pre-pass (the first draw of the frame to use it) read the previous frame's morph data from the GPU's
copy of guest memory, while the base and lighting passes - after another buffer had been bound to
that slot, forcing a request - read the current frame's. Wherever the face moved away from the camera
between frames, both colour passes failed their LESS_EQUAL depth test against the pre-pass and stayed
black. Xenia requests every vertex buffer on every draw. Every out-of-sync vertex buffer is requested
now; `gpu_vertex_buffer_state_skip = true` restores the old behaviour. Found by Gemini 4 Argon from the
handoff of this investigation; confirmed in play.

The investigation is kept below for reference.

## Investigation record

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
| Stale CPU-written data (facial animation vertices) | `clear_memory_page_state = true` (re-upload every CPU-written page each frame) |
| Lighting and post-processing patches | AO, bloom, lens flares, all post-processing and Option B off at once |
| Out-of-bounds vertex fetch reads, negative constant addresses | `sdk/patches/0041` (Xenia `9e9d3cdd3f`), `0042` (Xenia `674870d881`) |
| Xenos `0 * x = 0` rule in mul/mad/dp | already matches Xenia's translator |
| Bad shader constants (NaN/inf) | `debug_constant_nan` (patch 0043): none; only Unreal's FLT_MAX / 1e8 sentinels |
| XPS (CPU-to-GPU vertex streaming at 0x7F100000) | the game uses it, but the face draws read ordinary memory (trace with patch 0044) |
| Morph vertex data and its decoding | dumped with patch 0046: finite, small offsets; big-endian byte order confirmed from the base normals' sign byte |
| CPU vector pack/convert instructions (`vpk*`, `vct*`, `vcfp*`, `vcsx/vcux*`, `vupkd3d128`, `vpkd3d128`) | generated code checked against their semantics |
| Facial bone matrices and bone streams | Marcus's face (`F6C624B7...` lit, `DD51252A...` base; vertex shaders `ECFC0798...`, `9DD09713...`) skins from up to 70 facial bones (extra stream `vf2`) plus a morph stream (`vf3`); all 70 matrices sane in every dumped draw |
| The face vertex shader's output | run on the CPU interpreter (patch 0048) over all 13,869 vertices with the real inputs: every output finite and in range, so the inputs and shader logic are fine and the GPU translation of these shaders is the suspect |
| Render target path, re-tested | RTV after the depth pre-pass was found: no change |
| Driver fusing multiply-adds | all vertex shader arithmetic marked precise in DXBC: no change |
| A CPU/GPU race on the morph buffer | vertex buffer checksums identical for every pass of a frame |

### What the frame traces show

- While a cutscene plays, the face materials switch to morph-target vertex shaders (e.g. `D9D22C06...`,
  `90E49469...`; paused: `BCAC0D1E...`, `B4D9A140...`). They read a double-buffered, CPU-written morph stream
  (16 bytes a vertex: position offset as three floats, normal offset as 8:8:8:8).

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

### Where it stands (2026-10-23)

- It is a **depth-test rejection**: the face's lighting pass (`F6C624B7...`, LESS_EQUAL, no write) is
  rejected in the blotch regions; forcing its depth test to ALWAYS (`debug_depth_always.txt`) removes
  almost all of them. Its depth is compared with a depth-only pre-pass (`0856804D...`, EDRAM mode 5)
  and the base pass (`9DD09713...`).
- The mismatch is larger than rounding: a polygon offset of ~256 24-bit steps only reduces it.
- Vertex data, index buffers, bone matrices and position constants are identical for all three
  passes, and the CPU interpreter gives bit-identical positions for all of them. So the GPU produces
  a different depth from identical inputs for at least one pass.

### Ideas not yet tried

1. Compare the face draw's vertex data and constants between this build and Xenia. That needs a
   frame trace from both: Xenia's GPU > Trace Frame writes `scratch\gpu\*.xtr` (the folder must
   exist), but the ReXGlue runtime has no trace writer yet. Different data = CPU-side (recompiled
   VMX128 math, e.g. facial animation); same data = GPU runtime.
2. Review the recompiler's VMX128 implementations used by the animation code against Xenia's
   (`vpkd3d128`/`vupkd3d128` FLOAT16 and NORMSHORT paths looked correct on a first read).
3. Report to the ReXGlue SDK and the Gears 2 port, which share this runtime.
