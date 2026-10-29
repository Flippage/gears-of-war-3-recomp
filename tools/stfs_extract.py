"""Extracts the files of an Xbox 360 STFS package (CON / LIVE / PIRS), such as a title update.

Usage: python -I tools/stfs_extract.py <package> <output folder>

Reads the package's file table and follows each file's block chain through the hash tables.
Only for packages you own (bring your own game, title update and DLC); nothing is downloaded.
Format notes: the Free60 STFS documentation, and Xenia's stfs_container_device.cc.
"""

import os
import struct
import sys

BLOCK = 0x1000
HASHES_PER_TABLE = 0xAA


class Stfs:
    def __init__(self, data):
        self.data = data
        self.magic = data[:4]
        header_size = struct.unpack_from('>I', data, 0x340)[0]
        self.base = ((header_size + 0xFFF) & ~0xFFF)
        desc = 0x379
        self.desc_size = data[desc]
        # Volume descriptor: block separation, file table block count / number, top hash table.
        self.block_sep = data[desc + 2]
        self.ft_blocks = struct.unpack_from('<H', data, desc + 3)[0]
        self.ft_start = int.from_bytes(data[desc + 5:desc + 8], 'little')
        self.read_only = self.block_sep & 1
        # Packages created read only (LIVE / PIRS) use one hash table per level.
        self.tables_per_level = 1 if self.read_only else 2

    def block_offset(self, block):
        # Data block number -> file offset, skipping the hash table blocks interleaved with data:
        # one more table per level for every 170^(level+1) blocks (Xenia's BlockToOffsetSTFS).
        backing = block
        step = HASHES_PER_TABLE
        for _ in range(3):
            backing += (block + step) // step * self.tables_per_level
            if block < step:
                break
            step *= HASHES_PER_TABLE
        return self.base + backing * BLOCK

    def hash_entry(self, block):
        # Level-0 hash table entry for the block: 0x18 bytes (sha1, status, next block).
        table_block = (block // HASHES_PER_TABLE) * (HASHES_PER_TABLE + self.tables_per_level)
        if block >= HASHES_PER_TABLE:
            table_block += ((block // (HASHES_PER_TABLE ** 2)) + 1) * self.tables_per_level
        off = self.base + table_block * BLOCK + (block % HASHES_PER_TABLE) * 0x18
        nxt = int.from_bytes(self.data[off + 0x15:off + 0x18], 'big')
        return nxt

    def read_chain(self, start, count, size):
        out = bytearray()
        block = start
        for _ in range(count):
            off = self.block_offset(block)
            out += self.data[off:off + BLOCK]
            block = self.hash_entry(block)
        return bytes(out[:size])

    def entries(self):
        table = self.read_chain(self.ft_start, self.ft_blocks, self.ft_blocks * BLOCK)
        result = []
        for i in range(0, len(table), 0x40):
            e = table[i:i + 0x40]
            name_len = e[0x28] & 0x3F
            if name_len == 0:
                break
            name = e[:name_len].decode('latin-1')
            flags = e[0x28]
            blocks = int.from_bytes(e[0x29:0x2C], 'little')
            start = int.from_bytes(e[0x2F:0x32], 'little')
            parent = struct.unpack_from('>h', e, 0x32)[0]
            size = struct.unpack_from('>I', e, 0x34)[0]
            result.append(dict(name=name, dir=bool(flags & 0x80), contiguous=bool(flags & 0x40),
                               blocks=blocks, start=start, parent=parent, size=size))
        return result


def main():
    package, out_dir = sys.argv[1], sys.argv[2]
    data = open(package, 'rb').read()
    stfs = Stfs(data)
    if stfs.magic not in (b'CON ', b'LIVE', b'PIRS'):
        sys.exit('not an STFS package: %r' % stfs.magic)
    entries = stfs.entries()

    def path_of(i):
        parts = []
        while i >= 0:
            parts.append(entries[i]['name'])
            i = entries[i]['parent']
            if i == -1 or i == 0xFFFF:
                break
        return os.path.join(*reversed(parts))

    for i, e in enumerate(entries):
        p = os.path.join(out_dir, path_of(i))
        if e['dir']:
            os.makedirs(p, exist_ok=True)
            continue
        os.makedirs(os.path.dirname(p) or out_dir, exist_ok=True)
        content = stfs.read_chain(e['start'], e['blocks'], e['size'])
        with open(p, 'wb') as f:
            f.write(content)
        print('%10d  %s' % (e['size'], path_of(i)))


if __name__ == '__main__':
    main()
