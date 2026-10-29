"""Finds the Title Update 6 address of each base-game address our code uses.

Usage: python -I tools/map_tu6_addresses.py [address ...]

Reads the codegen output of both versions (generated/default and generated/tu6). Each function is
fingerprinted by its instructions (the `// insn` comments) with absolute addresses, branch
targets and `lis`/`addi` address halves blanked, so a function that only moved matches. An address
inside a function maps to the same offset in the matched function, after checking that the
instruction there is the same. Without arguments it maps every address found in src/ and
tools/patch_game_patches.py. Prints base -> tu6, how it matched, and anything ambiguous or missing.
"""

import glob
import hashlib
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FUNC_RE = re.compile(r'^(?:PPC_FUNC_IMPL\(__imp__|DEFINE_REX_FUNC\()sub_([0-9A-F]{8})\)')
INSN_RE = re.compile(r'^\s*// ([a-z][a-z0-9.+-]*)(?:\s+(.*))?$')
ADDR_RE = re.compile(r'0x8[0-9a-fA-F]{7}|sub_8[0-9A-Fa-f]{7}|loc_8[0-9A-Fa-f]{7}')


def normalize(op, args):
    args = args or ''
    args = ADDR_RE.sub('A', args)
    # lis / addis carry the high half of an address, the following addi / lwz / stw the low half.
    if op in ('lis', 'addis'):
        args = re.sub(r',-?\d+$', ',H', args)
    args = re.sub(r'(-?\d{4,})(?=\()', 'L', args)  # large displacements off an address register
    # ... and the low half of an address: a large addi / ori / addic immediate.
    if op in ('addi', 'ori', 'addic', 'subfic'):
        args = re.sub(r',-?\d{3,}$', ',I', args)
    return op + ' ' + args


def load(version):
    funcs = {}
    for path in glob.glob(os.path.join(ROOT, 'generated', version, '*_recomp.*.cpp')):
        cur = None
        with open(path, encoding='utf-8', errors='replace') as f:
            for line in f:
                m = FUNC_RE.match(line)
                if m:
                    cur = int(m.group(1), 16)
                    funcs[cur] = []
                    continue
                if cur is None:
                    continue
                m = INSN_RE.match(line)
                if m:
                    funcs[cur].append((m.group(1), m.group(2) or ''))
    return funcs


def fingerprint(insns):
    h = hashlib.sha1()
    for op, args in insns:
        h.update(normalize(op, args).encode())
        h.update(b'\n')
    return h.hexdigest()


def our_addresses():
    found = set()
    files = glob.glob(os.path.join(ROOT, 'src', '*.cpp')) + glob.glob(os.path.join(ROOT, 'src', '*.h'))
    files.append(os.path.join(ROOT, 'tools', 'patch_game_patches.py'))
    for path in files:
        text = open(path, encoding='utf-8', errors='replace').read()
        for m in re.finditer(r'(?:sub_|0x|\b)(8[23][0-9A-Fa-f]{6})\b', text):
            found.add(int(m.group(1), 16))
    return sorted(found)


def main():
    base, tu6 = load('default'), load('tu6')
    base_starts, tu6_starts = sorted(base), sorted(tu6)
    by_print = {}
    for addr, insns in tu6.items():
        by_print.setdefault(fingerprint(insns), []).append(addr)

    def containing(starts, funcs, addr):
        import bisect
        i = bisect.bisect_right(starts, addr) - 1
        if i < 0:
            return None
        start = starts[i]
        if addr < start + 4 * len(funcs[start]):
            return start
        return None

    base_prints = {}
    for addr, insns in base.items():
        base_prints.setdefault(fingerprint(insns), []).append(addr)
    unique = sorted((a, by_print[fp][0] - a) for fp, addrs in base_prints.items()
                    if len(addrs) == 1 and len(by_print.get(fp, [])) == 1 for a in addrs)

    def local_delta(addr):
        import bisect
        i = bisect.bisect_right(unique, (addr, 1 << 40)) - 1
        return unique[i][1] if i >= 0 else None

    wanted = [int(a, 16) for a in sys.argv[1:]] or our_addresses()
    same = moved = changed = missing = 0
    for addr in wanted:
        fn = containing(base_starts, base, addr)
        if fn is None:
            print('%08X  ?         not inside a base function (data or a constant?)' % addr)
            missing += 1
            continue
        off = addr - fn
        fp = fingerprint(base[fn])
        cands = by_print.get(fp, [])
        if fn in tu6 and fingerprint(tu6[fn]) == fp:
            print('%08X  %08X  same address (sub_%08X+%X)' % (addr, addr, fn, off))
            same += 1
        elif len(cands) == 1:
            new = cands[0] + off
            print('%08X  %08X  moved (sub_%08X -> sub_%08X, +%X)' % (addr, new, fn, cands[0], off))
            moved += 1
        elif len(cands) > 1:
            # Several identical functions: take the one nearest the shift of the closest uniquely
            # matched function before this one.
            delta = local_delta(fn)
            best = min(cands, key=lambda c: abs(c - (fn + delta))) if delta is not None else None
            if best is not None and abs(best - (fn + delta)) < 0x400:
                print('%08X  %08X  moved, nearest of %d (sub_%08X -> sub_%08X, +%X)' % (addr, best + off, len(cands), fn, best, off))
                moved += 1
            else:
                print('%08X  ?         ambiguous: sub_%08X matches %s' % (addr, fn, ' '.join('%08X' % c for c in cands)))
                missing += 1
        else:
            print('%08X  ?         changed in TU6: sub_%08X (%d insns) has no identical function' % (addr, fn, len(base[fn])))
            changed += 1
    print('\n%d same, %d moved, %d changed, %d unresolved' % (same, moved, changed, missing))


if __name__ == '__main__':
    main()
