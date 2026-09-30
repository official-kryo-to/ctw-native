# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kryo.to
# See LICENSE in the repository root.
# world.bin (ROM.WAD): collision/sector data, decoded from cWorld::Init / UpdateStreaming / cWorldSector::DataLoaded.
import struct, zlib, sys
sys.path.insert(0, 'tools')
import wad

def load_world(path_toc='data/rom.toc', path_wad='data/rom.wad'):
    toc = wad.load_toc(path_toc)
    o, s = toc[wad.name_hash('world.bin')]
    with open(path_wad, 'rb') as f:
        f.seek(o); return f.read(s)

def cell_raw(w, cx, cy):
    e = struct.unpack_from('<I', w, (cx * 100 + cy) * 4)[0]
    off, csz, extra = e & 0x1ffff, (e >> 17) & 0x7f, (e >> 24) & 0x7f
    if csz == 0: return None
    comp = w[off * 64: off * 64 + csz * 64]
    return zlib.decompressobj().decompress(comp)

SECTIONS = ['boxes', 'cylinders', 'meshes', 's108', 'pickups', 's118', 's120', 'cargens', 'emitters', 'attractors', 'ground', 's148']

def sections(d):
    out = {}; p = 0
    for name in SECTIONS[:-1]:
        n = struct.unpack_from('<I', d, p)[0]; out[name] = d[p + 4: p + 4 + n] if n else b''; p += 4 + n
    n = struct.unpack_from('<I', d, p)[0]; out['s148'] = d[p + 4:] if n else b''
    return out

def cell_of(x, y):
    return int((x + 3500) // 50), int((y + 2500) // 50)
