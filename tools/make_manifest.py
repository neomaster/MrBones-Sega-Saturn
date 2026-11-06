#!/usr/bin/env python3
"""Generate manifest/versions.txt from the user's own disc images.

The manifest holds hashes, sizes and LBAs only; no game data. Values describe the dumps
that were present when this was run (they are not an official redump database).

usage: make_manifest.py DISC1.cue_or_bin DISC2.cue_or_bin OUT.txt
"""
import hashlib, struct, sys, os

RAW, USER = 2352, 2048


def open_data_track(path):
    if path.lower().endswith('.cue'):
        base = os.path.dirname(path)
        for line in open(path, encoding='latin1'):
            t = line.strip()
            if t.upper().startswith('FILE'):
                name = t.split('"')[1]
                return os.path.join(base, name)
        raise SystemExit('no FILE in cue: ' + path)
    return path


class Disc:
    def __init__(self, path):
        self.path = open_data_track(path)
        self.f = open(self.path, 'rb')

    def sector(self, lba):
        self.f.seek(lba * RAW + 16)
        return self.f.read(USER)

    def read(self, lba, size):
        out = bytearray()
        for p in range(0, size, USER):
            out += self.sector(lba + p // USER)
        return bytes(out[:size])

    def walk(self, lba, size, prefix, items, seen):
        if (lba, size) in seen:
            return
        seen.add((lba, size))
        d = self.read(lba, size)
        p = 0
        while p < size:
            n = d[p]
            if n == 0:
                p = (p // USER + 1) * USER
                continue
            if n < 34 or p + n > size:
                raise SystemExit('bad directory record')
            idlen = d[p + 32]
            ident = d[p + 33:p + 33 + idlen]
            if ident not in (b'\x00', b'\x01'):
                name = ident.decode('ascii').split(';')[0]
                rl, rs = struct.unpack_from('<I', d, p + 2)[0], struct.unpack_from('<I', d, p + 10)[0]
                isdir = bool(d[p + 25] & 2)
                items.append((prefix + '/' + name, rl, rs, isdir))
                if isdir:
                    self.walk(rl, rs, prefix + '/' + name, items, seen)
            p += n


def sha(data):
    return hashlib.sha256(data).hexdigest()


def file_sha(disc, lba, size):
    h = hashlib.sha256()
    for off in range(0, size, 1 << 20):
        n = min(1 << 20, size - off)
        h.update(disc.read(lba + off // USER, n))
    return h.hexdigest()


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    discs = [Disc(sys.argv[1]), Disc(sys.argv[2])]
    lines = ['# mr-bones-port version manifest v1 (hashes of the maintainer\'s dumps; no game data)']
    header = None
    for n, d in enumerate(discs, 1):
        ip = d.sector(0)
        assert ip[:16] == b'SEGA SEGASATURN ', 'not a Saturn disc'
        prod = ip[0x20:0x2A].decode().strip()
        ver = ip[0x2A:0x30].decode().strip()
        date = ip[0x30:0x38].decode().strip()
        dev = ip[0x38:0x40].decode().strip()
        area = ip[0x40:0x41].decode().strip()
        assert dev == 'CD-%d/2' % n, 'disc %d reports %s' % (n, dev)
        cur = (prod, ver, date, area)
        assert header in (None, cur), 'discs disagree on product/version'
        header = cur
    lines.append('product %s %s %s %s' % header)
    for n, d in enumerate(discs, 1):
        h = hashlib.sha256()
        with open(d.path, 'rb') as f:
            for chunk in iter(lambda: f.read(1 << 20), b''):
                h.update(chunk)
        lines.append('disc %d track01 %d %s' % (n, os.path.getsize(d.path), h.hexdigest()))
    for n, d in enumerate(discs, 1):
        pvd = d.sector(16)
        assert pvd[1:6] == b'CD001'
        rl, rs = struct.unpack_from('<I', pvd, 158)[0], struct.unpack_from('<I', pvd, 166)[0]
        items = []
        d.walk(rl, rs, '', items, set())
        for path, lba, size, isdir in sorted(items):
            if isdir:
                continue
            base = path.rsplit('/', 1)[-1]
            core = path == '/02' or base in ('03CODE', '02TITL')
            lines.append('file %d %s %d %d %s %s' % (n, 'core' if core else 'extra', lba, size, file_sha(d, lba, size), path))
    open(sys.argv[3], 'w', newline='\n').write('\n'.join(lines) + '\n')
    print('wrote', sys.argv[3], len(lines), 'lines')


if __name__ == '__main__':
    main()
