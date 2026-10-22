#!/usr/bin/env python3
"""Read an Xbox 360 UE3 Coalesced_*.bin (the game's merged .ini files).

Layout (big-endian): int32 file count, then per file an FString name, int32 section count, per
section an FString name, int32 entry count, per entry an FString key and FString value. An FString
is an int32 length (negative: that many UTF-16LE code units including the terminator; positive:
that many bytes of Latin-1 including the terminator) followed by the characters.

Usage:
  python tools/coalesced.py <Coalesced_INT.bin> [section-substring] [key-substring]
"""
import struct
import sys


class Reader:
    def __init__(self, data: bytes):
        self.d, self.p = data, 0

    def i32(self) -> int:
        v = struct.unpack_from(">i", self.d, self.p)[0]
        self.p += 4
        return v

    def fstr(self) -> str:
        n = self.i32()
        if n == 0:
            return ""
        if n < 0:
            raw = self.d[self.p:self.p - 2 * n]
            self.p -= 2 * n
            return raw.decode("utf-16-le")[:-1]
        raw = self.d[self.p:self.p + n]
        self.p += n
        return raw.decode("latin-1")[:-1]


def parse(data: bytes):
    r = Reader(data)
    files = []
    for _ in range(r.i32()):
        name = r.fstr()
        sections = []
        for _ in range(r.i32()):
            sec = r.fstr()
            entries = [(r.fstr(), r.fstr()) for _ in range(r.i32())]
            sections.append((sec, entries))
        files.append((name, sections))
    return files


def main(argv):
    files = parse(open(argv[0], "rb").read())
    sec_filter = argv[1].lower() if len(argv) > 1 else None
    key_filter = argv[2].lower() if len(argv) > 2 else None
    if not sec_filter:
        for name, sections in files:
            print(f"{name}: {len(sections)} sections")
        return
    for name, sections in files:
        for sec, entries in sections:
            if sec_filter not in sec.lower():
                continue
            shown = [(k, v) for k, v in entries if not key_filter or key_filter in k.lower()]
            if shown:
                print(f"{name} [{sec}]")
                for k, v in shown:
                    print(f"    {k}={v}")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    main(sys.argv[1:])
