# Adapted from santiquiroz/gears-of-war-2-pc-port tools/harvest_hints.py (BSD-3-Clause, see THIRD_PARTY_NOTICES.md).
# Find guest entry points codegen missed and emit them as manifest hints.
import argparse
import bisect
import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

BASE = 0x82000000
SOURCE_ORDER = ("reloc", "thunk", "pdata", "datascan", "codeptr")  # spec example joins reloc before pdata
REASONS = ("denied", "not_code", "registered", "label", "inside_span", "zero", "mid_function")
DEFAULT_SOURCES = "thunk,pdata,datascan,codeptr,reloc"
BLR = 0x4E800020
BCTR = 0x4E800420


@dataclass
class Block:
    name: str
    start: int
    exec: bool
    data: bytes

    def contains(self, addr: int) -> bool:
        return self.start <= addr <= self.start + len(self.data) - 4

    def word(self, addr: int) -> int | None:
        if not self.contains(addr):
            return None
        off = addr - self.start
        return int.from_bytes(self.data[off:off + 4], "big")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Harvest missed guest entry points as manifest hints.")
    p.add_argument("--image", default="out/image")
    p.add_argument("--generated", default="out/baseline/default")
    p.add_argument("--out", default="hints/harvested.toml")
    p.add_argument("--report", default=None)
    p.add_argument("--deny", default="hints/denylist.txt")
    p.add_argument("--sources", default=DEFAULT_SOURCES,
                   help="comma list of sources that may produce accepted hints")
    return p.parse_args(argv)


def load_blocks(image: Path) -> list[Block]:
    blocks = []
    for line in (image / "blocks.tsv").read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        name, start, _size, _r, _w, x = (f.strip() for f in line.split("\t"))
        addr = int(start, 16)
        blocks.append(Block(name, addr, x == "true", (image / f"{addr:08X}.bin").read_bytes()))
    return blocks


def find_block(blocks: list[Block], addr: int, want_exec: bool | None = None) -> Block | None:
    for b in blocks:
        if want_exec is not None and b.exec != want_exec:
            continue
        if b.contains(addr):
            return b
    return None


def load_registered(generated: Path) -> set[int]:
    text = (generated / "gow3_register.cpp").read_text()
    return {int(m, 16) for m in re.findall(r"SetFunction\(0x([0-9A-Fa-f]+)", text)}


def load_labels(generated: Path) -> set[int]:
    labels: set[int] = set()
    for f in sorted(generated.glob("*_recomp.*.cpp")):
        labels.update(int(m, 16) for m in re.findall(r"^loc_([0-9A-Fa-f]{8}):", f.read_text(), re.M))
    return labels


def load_spans(generated: Path) -> list[tuple[int, int]]:
    func_re = re.compile(r"DEFINE_REX_FUNC\(sub_([0-9A-Fa-f]+)\)")
    spans: list[tuple[int, int]] = []
    for f in sorted(generated.glob("*_recomp.*.cpp")):
        text = f.read_text()
        matches = list(func_re.finditer(text))
        for i, m in enumerate(matches):
            start = int(m.group(1), 16)
            end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
            body = text[m.end():end]
            labels = [int(x, 16) for x in re.findall(r"^loc_([0-9A-Fa-f]{8}):", body, re.M)]
            if labels:
                mx = max(labels)
                if mx > start:
                    spans.append((start, mx))
    spans.sort()
    return spans


def is_inside_span(spans: list[tuple[int, int]], starts: list[int], addr: int) -> bool:
    i = bisect.bisect_left(starts, addr) - 1
    return i >= 0 and addr <= spans[i][1]


def reloc_headers_ok(data: bytes, endian: str) -> bool:
    fmt = ">" if endian == "big" else "<"
    off = 0
    for _ in range(3):
        if off + 8 > len(data):
            return False
        va, size = struct.unpack_from(fmt + "II", data, off)
        if va % 0x1000 or va >= 0x02000000 or size < 8 or size > 0x10000 or size % 2:
            return False
        off += size
    return True


def detect_reloc_endian(data: bytes) -> str:
    if reloc_headers_ok(data, "big"):
        return "big"
    return "little" if reloc_headers_ok(data, "little") else "big"


def harvest_reloc(blocks: list[Block], endian: str) -> tuple[dict[int, int], list[int], int]:
    reloc = next(b for b in blocks if b.name == ".reloc")
    fmt = ">" if endian == "big" else "<"
    data = reloc.data
    by_type: dict[int, int] = {}
    found: list[int] = []
    exec_ignored = 0
    off = 0
    while off + 8 <= len(data):
        va, size = struct.unpack_from(fmt + "II", data, off)
        if size < 8:
            break
        for i in range((size - 8) // 2):
            pos = off + 8 + 2 * i
            if pos + 2 > len(data):
                break
            (entry,) = struct.unpack_from(fmt + "H", data, pos)
            by_type[entry >> 12] = by_type.get(entry >> 12, 0) + 1
            if entry >> 12 != 3:
                continue
            slot = BASE + va + (entry & 0xFFF)
            if find_block(blocks, slot, want_exec=True) is not None:
                exec_ignored += 1
                continue
            target = find_block(blocks, slot, want_exec=False)
            if target is not None:
                val = target.word(slot)
                if val is not None:
                    found.append(val)
        off += size
    return by_type, found, exec_ignored


def is_branch_no_link(word: int) -> bool:
    return word >> 26 == 18 and word & 3 == 0


def harvest_thunks(blocks: list[Block]) -> list[int]:
    found = []
    for b in blocks:
        if not b.exec:
            continue
        for off in range(0, len(b.data) - 7, 4):
            if int.from_bytes(b.data[off:off + 4], "big") & 0xFFFF0000 != 0x38630000:
                continue
            if is_branch_no_link(int.from_bytes(b.data[off + 4:off + 8], "big")):
                found.append(b.start + off)
    return found


def harvest_datascan(blocks: list[Block]) -> list[int]:
    found = []
    for b in blocks:
        if b.exec or b.name in (".pdata", ".reloc"):
            continue
        for off in range(0, len(b.data) - 3, 4):
            (val,) = struct.unpack_from(">I", b.data, off)
            if val % 4:
                continue
            if find_block(blocks, val, want_exec=True) is not None:
                found.append(val)
    return found


def harvest_codeptr(blocks: list[Block]) -> list[int]:
    found = []
    for b in blocks:
        if not b.exec:
            continue
        n = len(b.data) // 4
        if n == 0:
            continue
        words = struct.unpack(f">{n}I", b.data[:4 * n])
        for i, w in enumerate(words):
            if w >> 26 != 15 or ((w >> 16) & 31) != 0:
                continue
            rd = (w >> 21) & 31
            hi = w & 0xFFFF
            for k in range(1, 9):
                j = i + k
                if j >= n:
                    break
                w2 = words[j]
                if w2 >> 26 == 15 and ((w2 >> 16) & 31) == 0 and ((w2 >> 21) & 31) == rd:
                    break
                val = None
                if w2 >> 26 == 14 and ((w2 >> 16) & 31) == rd:
                    lo = w2 & 0xFFFF
                    lo_signed = lo - 0x10000 if lo & 0x8000 else lo
                    val = ((hi << 16) + lo_signed) & 0xFFFFFFFF
                elif w2 >> 26 == 24 and ((w2 >> 21) & 31) == rd:
                    val = ((hi << 16) | (w2 & 0xFFFF)) & 0xFFFFFFFF
                else:
                    continue
                if val % 4:
                    continue
                if find_block(blocks, val, want_exec=True) is not None:
                    found.append(val)
    return found


def harvest_pdata(blocks: list[Block]) -> list[int]:
    pdata = next(b for b in blocks if b.name == ".pdata")
    found = []
    for off in range(0, len(pdata.data) - 7, 8):
        (begin,) = struct.unpack_from(">I", pdata.data, off)
        if begin == 0:
            break
        found.append(begin)
    return found


def add_candidates(cands: dict[int, set[str]], addrs: list[int], source: str) -> None:
    for a in addrs:
        cands.setdefault(a, set()).add(source)


def is_terminator(word: int) -> bool:
    return word in (BLR, BCTR, 0) or (word >> 26 == 18 and word & 1 == 0)


def has_code_boundary(blk: Block, addr: int) -> bool:
    if addr - 4 <= blk.start:
        return True
    prev = blk.word(addr - 4)
    return prev is not None and is_terminator(prev)


def load_denylist(path: Path) -> set[int]:
    try:
        text = path.read_text()
    except FileNotFoundError:
        return set()
    denied: set[int] = set()
    for line in text.splitlines():
        code = line.split("#", 1)[0].strip()
        if not code:
            continue
        try:
            denied.add(int(code.split()[0], 16))
        except ValueError:
            continue
    return denied


def classify(blocks: list[Block], cands: dict[int, set[str]],
             registered: set[int], labels: set[int],
             denied: set[int] | None = None,
             spans: list[tuple[int, int]] | None = None,
             allowed_sources: set[str] | None = None) -> tuple[dict[int, set[str]], dict[str, int]]:
    denied = denied or set()
    spans = spans or []
    starts = [s for s, _ in spans]
    accepted: dict[int, set[str]] = {}
    rejects = {r: 0 for r in REASONS}
    for addr, sources in cands.items():
        eff = sources & allowed_sources if allowed_sources is not None else sources
        if allowed_sources is not None and not eff:
            continue
        if addr in denied:
            rejects["denied"] += 1
            continue
        blk = find_block(blocks, addr, want_exec=True) if addr % 4 == 0 else None
        if blk is None:
            rejects["not_code"] += 1
        elif addr in registered:
            rejects["registered"] += 1
        elif addr in labels:
            rejects["label"] += 1
        elif "pdata" not in eff and is_inside_span(spans, starts, addr):
            rejects["inside_span"] += 1
        elif blk.word(addr) == 0:
            rejects["zero"] += 1
        elif "pdata" in eff or has_code_boundary(blk, addr):
            accepted[addr] = eff
        else:
            rejects["mid_function"] += 1
    return accepted, rejects


def format_addr(addr: int) -> str:
    return f"0x{addr:08X}"


def format_sources(sources: set[str]) -> str:
    return ",".join(s for s in SOURCE_ORDER if s in sources)


def write_toml(out: Path, accepted: dict[int, set[str]]) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    lines = ["# Generated by tools/harvest_hints.py - do not edit by hand.", "[functions]"]
    lines += [f"{format_addr(a)} = {{}}  # {format_sources(s)}" for a, s in sorted(accepted.items())]
    out.write_text("\n".join(lines) + "\n")


def format_summary(endian: str, by_type: dict[int, int], counts: dict[str, int],
                   unique: int, accepted: int, rejects: dict[str, int], exec_ignored: int) -> str:
    types = ", ".join(f"{t}: {by_type[t]}" for t in sorted(by_type)) or "(none)"
    cand = ", ".join(f"{s}={counts[s]}" for s in SOURCE_ORDER)
    rej = ", ".join(f"{r}={rejects[r]}" for r in REASONS)
    return "\n".join([
        f"reloc endianness: {endian}",
        f"reloc entries by type: {types}",
        f"reloc slots in executable blocks ignored: {exec_ignored}",
        f"candidates: {cand} (unique={unique})",
        f"accepted: {accepted}",
        f"rejected: {rej}",
    ])


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    blocks = load_blocks(Path(args.image))
    gen = Path(args.generated)
    if not (gen / "gow3_register.cpp").exists():
        print(f"error: {gen}/gow3_register.cpp not found; run scripts/build.ps1 first",
              file=sys.stderr)
        return 2
    registered = load_registered(gen)
    labels = load_labels(gen)
    spans = load_spans(gen)
    allowed = {s.strip() for s in args.sources.split(",") if s.strip()}
    endian = detect_reloc_endian(next(b for b in blocks if b.name == ".reloc").data)
    by_type, reloc_ptrs, exec_ignored = harvest_reloc(blocks, endian)
    cands: dict[int, set[str]] = {}
    if "reloc" in allowed:
        add_candidates(cands, reloc_ptrs, "reloc")
    if "thunk" in allowed:
        add_candidates(cands, harvest_thunks(blocks), "thunk")
    if "pdata" in allowed:
        add_candidates(cands, harvest_pdata(blocks), "pdata")
    if "datascan" in allowed:
        add_candidates(cands, harvest_datascan(blocks), "datascan")
    if "codeptr" in allowed:
        add_candidates(cands, harvest_codeptr(blocks), "codeptr")
    denied = load_denylist(Path(args.deny))
    accepted, rejects = classify(blocks, cands, registered, labels, denied, spans)
    counts = {s: sum(1 for v in cands.values() if s in v) for s in SOURCE_ORDER}
    write_toml(Path(args.out), accepted)
    summary = format_summary(endian, by_type, counts, len(cands), len(accepted), rejects, exec_ignored)
    print(summary)
    if args.report:
        report = Path(args.report)
        report.parent.mkdir(parents=True, exist_ok=True)
        lines = [summary, "accepted addresses:"]
        lines += [f"{format_addr(a)}: {format_sources(s)}" for a, s in sorted(accepted.items())]
        report.write_text("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
