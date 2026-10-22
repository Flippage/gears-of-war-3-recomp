"""Post-codegen game patches for Gears of War 3 (title 4D5308AB, disc v0.0.0.11).

Same approach as the Gears of War 2 port's and the Gears of War: Judgment recomp's
tools/patch_game_patches.py: a patch in a recompile has to land on the C++ that implements the
instruction, not on bytes, and each site keeps the original behaviour behind a host switch,
`Gow3GamePatch(id)` (src/gow3_game_patches.cpp), so every patch is a cvar.

Sources
  id 1 is the Gears of War 2 community patch "Black Shading Fix" (illusion, boma), located in
  Gears 3 by hand: the GoW2 site, stw r11,11940(r6) in the render defaults function sub_828069F8
  (the function just before its back-buffer setup), matches Gears 3's sub_82960B50 store by store
  (same "initialised" guard, same run of flags; GUseTilingMode is the fourth store, the third flag
  set to 1, stored from r11). The back-buffer setup sub_82960BE8 reads it; nothing else writes it.

  ids 2-10 come from xenia-canary/game-patches, patches/4D5308AB - Gears of War 3.patch.toml
  (the base disc; its media list includes 58122AD6, this disc, and its 21:9 patch edits the
  `li r11,720` at 82960C78 of the back-buffer setup above). Every site's original instruction was
  checked with tools/find_site.py against out/image/image.bin and the generated comment.

  id  name (cvar)                          site       original              -> patched
  1   Black Shading Fix                    82960B84   stw r11,-29808(r6)    -> stores 0
      (gow3_black_shading_fix)
  2   Unlock FPS (gow3_unlock_fps)         8253D9B0   lfs f1,1224(r3)       -> nop
                                           82AF66BC   li r11,2              -> li r11,1
  3   Occlusion Query Fix                  827B2BD0   lwz r11,2700(r28)     -> li r11,0
      (gow3_occlusion_query_fix)           827B6C58   lwz r11,2700(r14)     -> li r11,0
  4   Disable Lens Flares                  82861FD0   lwz r10,96(r11)       -> li r10,0
      (gow3_disable_lens_flares)
  5   Disable Ambient Occlusion            82762C54   lwz r11,1040(r10)     -> li r11,0
      (gow3_disable_ao)                    827C9628   lwz r11,1040(r10)     -> li r11,0
  6   Disable Motion Blur                  82430640   lwz r11,56(r10)       -> li r11,0
      (gow3_disable_motion_blur)           827A2BCC   lwz r11,128(r10)      -> li r11,0
  7   16x Anisotropic Filtering            82969614   lwz r5,836(r30)       -> li r5,16
      (gow3_aniso16)
  8   Resolution Scaling Fix / Disable     827B8764   bl 0x827b92c8         -> li r5,1
      All Post-Processing                  827B88C0   bl 0x827b92c8         -> li r5,0
      (gow3_resolution_scaling_fix)        827B8BA8   bl 0x827b92c8         -> li r4,4
  9   Disable Depth of Field               824BAA50   lwz r11,64(r10)       -> li r11,0
      (gow3_disable_dof)                   82593DC4   lwz r11,64(r11)       -> li r11,0
  10  Disable Bloom (gow3_disable_bloom)   82593DC0   lwz r10,72(r11)       -> li r10,0
  11  Texture pool size                    82967858   lwz r26,80(r1)        -> r26 = pool MB
      (gow3_texture_pool_mb)
      The Gears of War 2 port's gow2_texture_streaming_fix sets PoolSize=196 at the load after
      GConfig->GetInt(TextureStreaming, PoolSize) (8280D534 lwz r4,80(r1)). Gears 3's matching
      read is in sub_829677A8: bl 0x822a7920 with r3 = L"TextureStreaming" (8212F730), r4 =
      L"PoolSize" (8212F9D8), r5 = 80(r1); then the -POOLSIZE= command-line check (821CAD60), and
      without it r26 = the configured value (145 in Xbox360-GearEngine.ini). The Gears 2 patch's
      second half (RHIGetTextureMemoryStats returning early, FudgeFactor locked at 1.0) has no
      counterpart found in Gears 3 and is not ported.

  12  Performance Option B: Optimized     827D0878   addi r11,r16,1        -> code cave in C++: at most 20
      Settings (Smart Shadow Cuts)                                           per-object shadow depth draws a frame
      (gow3_shadow_perf_b)                 827CACC4   mr r21,r19            -> li r21,1 (no pre-allocated shadow
                                                                             caching, "blows up NVIDIA")
                                           829A138C   lwz r3,108(r10)       -> li r3,0 (OneFrameRenderLag = FALSE)
  13  No dynamic shadows (from Option A)   827CE810   lwz r11,1044(r21)     -> li r11,0
      (gow3_disable_dynamic_shadows)

  Not ported: Option A's byte at 8236C98C (0x00 there would turn lwz r10,196(r11) into an
  invalid instruction on this disc), Show FPS (data bytes) and 21:9.

    python tools/patch_game_patches.py

Idempotent (marker GOW3 GAMEPATCH). Fails closed if a site's disassembly comment or C++ body
differs from what is asserted.
"""
from __future__ import annotations

import bisect
import glob
import re
import sys
from pathlib import Path

MARK = "GOW3 GAMEPATCH"
DECL = f'extern "C" int Gow3GamePatch(int id);  // {MARK}\n'
GEN = "generated/default"

# (address, expected disassembly, patched form, id). Patched forms:
#   ("li", "r11", 0)   the instruction becomes li rD,imm
#   ("nop",)           the instruction is skipped
#   ("store0",)        a store writes 0 instead of the register
#   ("value", "r26")   the register gets Gow3GamePatch(id) when that is above 0, else the original
#   ("code", "...")    the instruction's C++ is replaced by the given C++ (which tests the switch)
SITES = [
    (0x82960B84, "stw r11,-29808(r6)", ("store0",), 1),
    (0x8253D9B0, "lfs f1,1224(r3)", ("nop",), 2),
    (0x82AF66BC, "li r11,2", ("li", "r11", 1), 2),
    (0x827B2BD0, "lwz r11,2700(r28)", ("li", "r11", 0), 3),
    (0x827B6C58, "lwz r11,2700(r14)", ("li", "r11", 0), 3),
    (0x82861FD0, "lwz r10,96(r11)", ("li", "r10", 0), 4),
    (0x82762C54, "lwz r11,1040(r10)", ("li", "r11", 0), 5),
    (0x827C9628, "lwz r11,1040(r10)", ("li", "r11", 0), 5),
    (0x82430640, "lwz r11,56(r10)", ("li", "r11", 0), 6),
    (0x827A2BCC, "lwz r11,128(r10)", ("li", "r11", 0), 6),
    (0x82969614, "lwz r5,836(r30)", ("li", "r5", 16), 7),
    (0x827B8764, "bl 0x827b92c8", ("li", "r5", 1), 8),
    (0x827B88C0, "bl 0x827b92c8", ("li", "r5", 0), 8),
    (0x827B8BA8, "bl 0x827b92c8", ("li", "r4", 4), 8),
    (0x824BAA50, "lwz r11,64(r10)", ("li", "r11", 0), 9),
    (0x82593DC4, "lwz r11,64(r11)", ("li", "r11", 0), 9),
    (0x82593DC0, "lwz r10,72(r11)", ("li", "r10", 0), 10),
    (0x82967858, "lwz r26,80(r1)", ("value", "r26"), 11),
    # Performance Option B: at most 20 per-object shadow depth draws a frame (whole-scene and
    # preshadows, flags 0x06000000 in 1248(r30), always kept); the cave reloads r10 from the
    # flags word the following rlwimi/stw use anyway, and r9/cr6 are overwritten before use.
    (0x827D0878, "addi r11,r16,1", ("code",
        "if (Gow3GamePatch(12)) { ctx.r10.u64 = REX_LOAD_U32(ctx.r30.u32 + 1248); "
        "ctx.r9.u64 = ctx.r10.u32 & 0x06000000u; "
        "if (ctx.r9.u32 == 0 && ctx.r16.s32 >= 20) goto loc_827D08D4; } "
        "ctx.r11.s64 = ctx.r16.s64 + 1;"), 12),
    (0x827CACC4, "mr r21,r19", ("li", "r21", 1), 12),
    (0x829A138C, "lwz r3,108(r10)", ("li", "r3", 0), 12),
    (0x827CE810, "lwz r11,1044(r21)", ("li", "r11", 0), 13),
]


def load_functions():
    funcs = []
    for path in glob.glob(f"{GEN}/gow3_recomp.*.cpp"):
        for m in re.finditer(r"^DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\) \{",
                             Path(path).read_text(encoding="utf-8"), re.M):
            funcs.append((int(m.group(1), 16), path))
    funcs.sort()
    return funcs


def locate(lines: list[str], fstart: int, addr: int):
    """(index of the comment line, index past its code) for addr inside the function at fstart."""
    head = next(i for i, l in enumerate(lines) if l.startswith(f"DEFINE_REX_FUNC(sub_{fstart:08X})"))
    cur = fstart
    i = head + 1
    while i < len(lines) and not lines[i].startswith("DEFINE_REX_FUNC("):
        s = lines[i].strip()
        lab = re.match(r"loc_([0-9A-F]{8}):", s)
        if lab:
            cur = int(lab.group(1), 16)
            i += 1
            continue
        if s.startswith("// "):
            j = i + 1
            while j < len(lines) and not lines[j].strip().startswith("//") \
                    and not re.match(r"\s*loc_[0-9A-F]{8}:", lines[j]) \
                    and not lines[j].startswith("DEFINE_REX_FUNC(") and lines[j].strip() != "}":
                j += 1
            if cur == addr:
                return i, j
            cur += 4
            i = j
            continue
        i += 1
    return None


def patched_code(form, code: list[str], sid: int, indent: str) -> list[str]:
    orig = " ".join(c.strip() for c in code if c.strip())
    cond = f"Gow3GamePatch({sid})"
    if form[0] == "li":
        return [f"{indent}if ({cond}) {{ ctx.{form[1]}.s64 = {form[2]}; }} else {{ {orig} }}  // {MARK} {sid}"]
    if form[0] == "nop":
        return [f"{indent}if (!{cond}) {{ {orig} }}  // {MARK} {sid}"]
    if form[0] == "code":
        return [f"{indent}{form[1]}  // {MARK} {sid}"]
    if form[0] == "value":
        return [f"{indent}if (const int v = {cond}; v > 0) {{ ctx.{form[1]}.u64 = uint32_t(v); }} else {{ {orig} }}  // {MARK} {sid}"]
    if form[0] == "store0":
        m = re.fullmatch(r"REX_STORE_U32\((.+), (ctx\.r\d+\.u32)\);", orig)
        if not m:
            raise ValueError(f"store0 expects one REX_STORE_U32, got: {orig}")
        return [f"{indent}REX_STORE_U32({m.group(1)}, {cond} ? 0u : {m.group(2)});  // {MARK} {sid}"]
    raise ValueError(form)


def main() -> int:
    funcs = load_functions()
    starts = [f[0] for f in funcs]
    changed: dict[str, list[str]] = {}
    count = 0
    for addr, expect, form, sid in SITES:
        fstart, path = funcs[bisect.bisect_right(starts, addr) - 1]
        lines = changed.get(path) or Path(path).read_text(encoding="utf-8").split("\n")
        where = locate(lines, fstart, addr)
        if not where:
            print(f"error: {addr:08X} not found in sub_{fstart:08X}", file=sys.stderr)
            return 1
        i, j = where
        comment = lines[i].strip()[3:]
        if comment != expect:
            print(f"error: {addr:08X} is '{comment}', expected '{expect}' (patch {sid}); refusing to guess",
                  file=sys.stderr)
            return 1
        code = lines[i + 1:j]
        if any(f"{MARK} {sid}" in c for c in code):
            count += 1
            continue
        indent = re.match(r"\s*", lines[i]).group(0)
        lines[i + 1:j] = patched_code(form, code, sid, indent)
        changed[path] = lines
        count += 1
        print(f"patch {sid}: {addr:08X} {expect} in {Path(path).name}")
    for path, lines in changed.items():
        text = "\n".join(lines)
        if DECL not in text:
            first = text.find("DEFINE_REX_FUNC(")
            text = text[:first] + DECL + "\n" + text[first:]
        Path(path).write_text(text, encoding="utf-8", newline="\n")
    print(f"game patches: {count}/{len(SITES)} sites patched")
    return 0


if __name__ == "__main__":
    sys.exit(main())
