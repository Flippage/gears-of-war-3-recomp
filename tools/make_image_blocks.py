#!/usr/bin/env python3
"""Split the GOW3_DUMP_IMAGE dump (out/image/image.bin, the loaded image at 0x82000000) into the
layout tools/harvest_hints.py reads (the one tools/ghidra/ExportImage.java writes in the Gears of
War 2 port): <dir>/blocks.tsv (name, start, size, r, w, x) and <dir>/<start>.bin per section.

Sections come from the PE section table at the start of the image.
Usage: python tools/make_image_blocks.py [out/image]
"""
import struct
import sys
from pathlib import Path

BASE = 0x82000000
IMAGE_SCN_MEM_EXECUTE = 0x20000000
IMAGE_SCN_MEM_READ = 0x40000000
IMAGE_SCN_MEM_WRITE = 0x80000000


def main() -> int:
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "out/image")
    image = (out / "image.bin").read_bytes()
    if image[:2] != b"MZ":
        sys.exit("image.bin does not start with an MZ header")
    pe = struct.unpack_from("<I", image, 0x3C)[0]
    if image[pe:pe + 4] != b"PE\0\0":
        sys.exit("no PE signature")
    sections, opt_size = struct.unpack_from("<H12xH", image, pe + 6)
    table = pe + 24 + opt_size
    rows = []
    for i in range(sections):
        name, vsize, vaddr, _raw_size, _raw_ptr, flags = struct.unpack_from("<8sIII I12xI", image, table + i * 40)
        name = name.rstrip(b"\0").decode("ascii", "replace")
        start = BASE + vaddr
        data = image[vaddr:vaddr + vsize]
        (out / f"{start:08X}.bin").write_bytes(data)
        r, w, x = (bool(flags & f) for f in (IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_WRITE, IMAGE_SCN_MEM_EXECUTE))
        rows.append(f"{name}\t{start:08X}\t{len(data)}\t{str(r).lower()}\t{str(w).lower()}\t{str(x).lower()}")
        print(f"{name:10} {start:08X} {len(data):>10}  {'r' if r else '-'}{'w' if w else '-'}{'x' if x else '-'}")
    (out / "blocks.tsv").write_text("\n".join(rows) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
