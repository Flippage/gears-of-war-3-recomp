#!/usr/bin/env python3
"""Map guest addresses to the generated C++ that implements them.

For each address: the function containing it (DEFINE_REX_FUNC(sub_X) with the largest X <= addr),
the instruction's disassembly comment and its C++ lines, found by walking the function's
"// <disasm>" comments one per 4 bytes and resyncing at every loc_XXXXXXXX label. The original
word from out/image/image.bin is printed too, so a site can be checked against the patch file.

Usage: python tools/find_site.py 0x82762c54 0x827c9628 ...
"""
import bisect
import glob
import re
import struct
import sys
from pathlib import Path

GEN = "generated/default"
IMAGE = Path("out/image/image.bin")
BASE = 0x82000000


def load_functions():
    funcs = []
    for path in glob.glob(f"{GEN}/gow3_recomp.*.cpp"):
        text = Path(path).read_text(encoding="utf-8")
        for m in re.finditer(r"^DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\) \{", text, re.M):
            funcs.append((int(m.group(1), 16), path, m.start()))
    funcs.sort()
    return funcs


def walk(path, start):
    """Yield (address, comment, [code lines]) for one function body."""
    text = Path(path).read_text(encoding="utf-8")
    end = text.find("\nDEFINE_REX_FUNC(", start + 1)
    body = text[start:end if end > 0 else len(text)].splitlines()
    addr = int(re.match(r"DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\)", body[0]).group(1), 16)
    i = 1
    while i < len(body):
        line = body[i].strip()
        lab = re.match(r"loc_([0-9A-F]{8}):", line)
        if lab:
            addr = int(lab.group(1), 16)
        elif line.startswith("// "):
            code = []
            j = i + 1
            while j < len(body) and not body[j].strip().startswith("//") and not re.match(r"\s*loc_[0-9A-F]{8}:", body[j]):
                if body[j].strip() and body[j].strip() != "}":
                    code.append(body[j].strip())
                j += 1
            yield addr, line[3:], code
            addr += 4
            i = j
            continue
        i += 1


def main(argv):
    funcs = load_functions()
    starts = [f[0] for f in funcs]
    image = IMAGE.read_bytes() if IMAGE.exists() else None
    for a in argv:
        addr = int(a, 16)
        k = bisect.bisect_right(starts, addr) - 1
        fstart, path, pos = funcs[k]
        word = struct.unpack_from(">I", image, addr - BASE)[0] if image else None
        hit = next(((c, code) for x, c, code in walk(path, pos) if x == addr), None)
        print(f"{addr:08X} word={word:08X} in sub_{fstart:08X} ({Path(path).name})")
        if hit:
            print(f"    // {hit[0]}")
            for c in hit[1]:
                print(f"    {c}")
        else:
            print("    (not found in the function body)")


if __name__ == "__main__":
    main(sys.argv[1:])
