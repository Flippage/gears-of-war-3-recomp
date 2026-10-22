"""Post-codegen game patches for Gears of War 3 (title 4D5308AB, disc v0.0.0.11).

Same approach as the Gears of War 2 port's and the Gears of War: Judgment recomp's
tools/patch_game_patches.py: a patch in a recompile has to land on the C++ that implements the
instruction, not on bytes, and each site keeps the original behaviour behind a host switch,
`Gow3GamePatch(id)` (src/gow3_game_patches.cpp), so every patch is a cvar.

  id 1 Black Shading Fix   82960B84 stw r11,-29808(r6) -> stores 0 (GUseTilingMode = FALSE)
                           (GameFixes/gow3_black_shading_fix=true)

     The Gears of War 2 community patch (illusion, boma) writes `li r11,0` over
     `stw r11,11940(r6)` in that game's render defaults function, sub_828069F8, the function just
     before its back-buffer setup. Gears 3's matching pair is sub_82960B50 (defaults) and
     sub_82960BE8 (back buffer, which calls it while uninitialised). Both defaults functions guard
     on the same "initialised" global and set the same run of flags; GUseTilingMode is the fourth
     store in each, the third flag set to 1, and the one stored from r11 - here -29808(r6). The
     back-buffer function reads it while laying out the EDRAM targets, and nothing else writes it.
     With tiling off, the in-engine cutscene shadows lose the black blocks that MSAA resolves left.

    python tools/patch_game_patches.py "generated/default/*.cpp"

Idempotent (marker GOW3 GAMEPATCH). Fails closed if a site's function, disassembly comment or
C++ body differs from what is asserted below.
"""
from __future__ import annotations

import glob
import re
import sys

MARK = "GOW3 GAMEPATCH"
DECL = f'extern "C" int Gow3GamePatch(int id);  // {MARK}\n'

# (function, expected comment, expected C++ body, patched C++ body, id)
SITES = [
    ("sub_82960B50", "stw r11,-29808(r6)",
     "REX_STORE_U32(ctx.r6.u32 + -29808, ctx.r11.u32);",
     f"REX_STORE_U32(ctx.r6.u32 + -29808, Gow3GamePatch(1) ? 0u : ctx.r11.u32);  // {MARK} 1", 1),
]


def function_span(text: str, name: str) -> tuple[int, int] | None:
    start = text.find(f"DEFINE_REX_FUNC({name})")
    if start < 0:
        return None
    nxt = text.find("DEFINE_REX_FUNC(", start + 1)
    return start, (nxt if nxt >= 0 else len(text))


def main(pattern: str) -> int:
    files = sorted(glob.glob(pattern))
    if not files:
        print(f"no files match {pattern}", file=sys.stderr)
        return 1
    done = 0
    for func, comment, body, patched, sid in SITES:
        hit = None
        for path in files:
            with open(path, encoding="utf-8") as f:
                text = f.read()
            span = function_span(text, func)
            if span:
                hit = (path, text, span)
                break
        if not hit:
            print(f"error: {func} not found (patch {sid})", file=sys.stderr)
            return 1
        path, text, (start, end) = hit
        region = text[start:end]
        if f"{MARK} {sid}" in region:
            done += 1
            continue
        site = re.search(rf"// {re.escape(comment)}\n(\s*){re.escape(body)}\n", region)
        if not site:
            print(f"error: {func}: '{comment}' / '{body}' not found as expected (patch {sid});"
                  " refusing to guess", file=sys.stderr)
            return 1
        new_region = region[:site.start()] + f"// {comment}\n{site.group(1)}{patched}\n" + region[site.end():]
        text = text[:start] + new_region + text[end:]
        if DECL not in text:
            first = text.find("DEFINE_REX_FUNC(")
            text = text[:first] + DECL + "\n" + text[first:]
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        done += 1
        print(f"patch {sid}: {func} {comment} in {path}")
    print(f"game patches: {done}/{len(SITES)} sites patched")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "generated/default/*.cpp"))
