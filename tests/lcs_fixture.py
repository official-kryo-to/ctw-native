# SPDX-License-Identifier: GPL-3.0-only
"""Entirely synthetic WAD/IMG/material fixture; no game data."""
import struct
import zipfile
import zlib
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from lcs_assets import WANTED, BIKE_BANKS


def chunk(kind, data):
    return struct.pack('<III', kind, len(data), 0x310) + data


def apk(path, package='com.rockstargames.gtalcs', version='2.4.379', bad_offset=False,
        bike_banks=BIKE_BANKS, bad_bike=False):
    geometry = bytearray(64)
    struct.pack_into('<I', geometry, 16, 64)
    texture = chunk(6, chunk(1, bytes(4)) + chunk(2, b'test\0') + chunk(2, b'\0'))
    material = chunk(7, chunk(1, bytes(28)) + texture)
    materials = chunk(8, chunk(1, struct.pack('<Ii', 1, -1)) + material)
    dff = chunk(16, chunk(20, chunk(1, bytes(16)) + chunk(15, bytes(geometry) + materials)))
    image, directory = bytearray(), bytearray()
    for name in sorted(WANTED):
        directory += struct.pack('<II24s', len(image)//2048, 1, (name+'.dff').encode())
        image += dff + bytes(2048-len(dff))
    for index,name in enumerate(bike_banks):
        bike=struct.pack('<4sI',b'ANPK',4096 if bad_bike else 4)+bytes((index,))*4
        directory += struct.pack('<II24s',len(image)//2048,1,name.encode())
        image += bike+bytes(2048-len(bike))
    files = {'models/gta3.img': bytes(image), 'models/gta3.dir': bytes(directory),
             'data/default.ide': ('peds\n'+''.join(f'{i},{n},{n},test\n' for i,n in enumerate(sorted(WANTED)))+'end\n').encode(),
             'anim/ped.ifp': b'synthetic animation',
             'master_texture_mapping_process.csv': ''.join(f'{n}/test,{n},test\n' for n in sorted(WANTED)).encode()}
    for name in WANTED:
        files[f'textures_etc/{name}/test.pvr'] = b'synthetic texture'
    fat, payload = bytearray(), bytearray()
    offset = 2056+48*len(files)
    for name,data in sorted(files.items()):
        fat += struct.pack('<4I4Q', zlib.crc32(name.encode())^0xffffffff, 0, 0, 0,
                           (1<<40) if bad_offset else offset+len(payload), len(data), len(data), 0)
        payload += data
    wad = bytearray(struct.pack('<III',0x4c574144,1,1)+bytes(2036)+struct.pack('<II',0,len(files))+fat+payload)
    for phase,key in ((0,0xaf),(1,0x66)):
        wad[phase::2] = bytes(wad[phase::2]).translate(bytes(b^key for b in range(256)))
    with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_STORED) as z:
        z.writestr('AndroidManifest.xml', f'<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="{package}" android:versionName="{version}"/>')
        z.writestr('assets/data_main.wad', wad)
