# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kryo.to
# See LICENSE in the repository root.
"""game.pak reader (verified: all 4180 entries tile the file exactly).
Header: u32 version, u32 seg1, u32 seg2, u32 seg3, u32 count, u32 endPage; u16 page table at 0x18.
The table only fits partly in the first 4 KB page; cResourceManager::Init reads the remainder from endPage*4096
(the file tail). Page = table[id] + 0x10000 * segment(id), segment from seg1/seg2/seg3; size = (table[id+1]-table[id]) & 0xFFFF pages."""
import struct
class PakFile:
    def __init__(s, path='port/data/game.pak'):
        s.f = open(path, 'rb'); h = s.f.read(0x1000)
        _, s.s1, s.s2, s.s3, s.count, s.end = struct.unpack_from('<6I', h, 0)
        hdr = (s.count * 2 + 0x1017) & ~0xFFF
        s.f.seek(s.end * 4096); buf = h + s.f.read(hdr - 0x1000)
        s.t = list(struct.unpack_from('<%dH' % s.count, buf, 0x18)) + [s.end & 0xFFFF]
    def page(s, i):
        seg = 0 if i < s.s1 else 1 if i < s.s2 else 2 if i < s.s3 else 3
        return s.t[i] + 0x10000 * seg
    def size(s, i):
        if s.t[i] == 0xFFFF: return 0
        return ((s.t[i + 1] - s.t[i]) & 0xFFFF) * 4096
    def read(s, i, n=None):
        sz = s.size(i)
        if not sz: return b''
        s.f.seek(s.page(i) * 4096); return s.f.read(min(sz, n) if n else sz)
# legacy helpers used by older scripts
def load_index(path): p = PakFile(path); return dict(tab=p.t, count=p.count, t1=p.s1, t2=p.s2, t3=p.s3, endpage=p.end, obj=p)
def page(ix, i): return ix['obj'].page(i)
