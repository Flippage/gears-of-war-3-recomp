"""Names the functions in a log's CRASHLOG lines, from the build's linker map.

Usage: python -I tools/crashlog_symbols.py <build folder> [log]

Reads <build folder>/gow3.map and the newest <build folder>/logs/gow3_*.log (or the given log) and
prints each CRASHLOG offset as the symbol that contains it: recompiled functions are sub_<guest
address>, so a frame names the game function it was in.
"""

import bisect
import glob
import os
import re
import sys


def load_map(path):
    base = 0x140000000
    entries = []
    with open(path, encoding='latin-1') as f:
        for line in f:
            if 'Preferred load address is' in line:
                base = int(line.split()[-1], 16)
            m = re.match(r'\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8,16})\s', line)
            if m:
                entries.append((int(m.group(2), 16) - base, m.group(1)))
    entries.sort()
    return entries


def main():
    build = sys.argv[1]
    log = sys.argv[2] if len(sys.argv) > 2 else max(glob.glob(os.path.join(build, 'logs', 'gow3_*.log')),
                                                    key=os.path.getmtime)
    entries = load_map(os.path.join(build, 'gow3.map'))
    starts = [e[0] for e in entries]
    for line in open(log, encoding='utf-8', errors='replace'):
        if 'CRASHLOG' not in line:
            continue
        m = re.search(r'gow3\.exe \+ 0x([0-9A-Fa-f]+)', line)
        text = line.split('CRASHLOG', 1)[1].strip()
        if m:
            off = int(m.group(1), 16)
            i = bisect.bisect_right(starts, off) - 1
            name = entries[i][1] if i >= 0 else '?'
            print('%-60s %s+0x%X' % (text[:60], name, off - entries[i][0] if i >= 0 else 0))
        else:
            print(text)


if __name__ == '__main__':
    main()
