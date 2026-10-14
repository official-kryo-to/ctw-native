# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kryo.to
"""Read user-supplied LCS 2.4.379 character resources. Never executes APK code.

WADL serialization and IMG offsets were checked against the Android reader.
Only pedestrians, their texture dictionaries/textures and ped.ifp are imported.
The Rust Porter implements the same selection and byte-for-byte extraction.
"""
import csv
import io
from pathlib import Path
import struct
import zipfile
import zlib
from apk_manifest import manifest_info

PACKAGE = 'com.rockstargames.gtalcs'
VERSION = '2.4.379'
LIMIT = 256 << 20
RESOURCE_LIMIT = 32 << 20
# Enhancements' replacement roster. Names are an import policy, not an extracted game table.
WANTED = {'player', 'cop', 'male01', 'male02', 'male03', 'b_man1', 'worker1', 'p_man1',
          'female01', 'female02', 'female03', 'b_wom1', 'shopper1', 'p_wom1'}
BIKE_BANKS = ('biked.ifp', 'bikeh.ifp', 'bikes.ifp', 'bikev.ifp')


def safe_name(value):
    reserved = {'con', 'prn', 'aux', 'nul', *(f'com{i}' for i in range(1, 10)), *(f'lpt{i}' for i in range(1, 10))}
    if not value or value in reserved or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789_-' for c in value):
        raise ValueError(f'Invalid LCS character resource name: {value!r}')
    return value


class Wad:
    def __init__(self, stream, length):
        self.stream, self.length = stream, length
        if self.read(0, 12) != struct.pack('<III', 0x4c574144, 1, 1):
            raise ValueError('Unsupported LCS WAD header')
        count = struct.unpack('<I', self.read(2048, 4))[0]
        if count > 65536:
            raise ValueError('Invalid LCS directory count')
        offset = 2052 + count * 8
        count = struct.unpack('<I', self.read(offset, 4))[0]
        if count > 65536:
            raise ValueError('Invalid LCS file count')
        self.files = {}
        for row in struct.iter_unpack('<4I4Q', self.read(offset + 4, count * 48)):
            key, _, _, _, at, size, unpacked, flags = row
            if key in self.files or at > length or size > length - at or size != unpacked or flags:
                raise ValueError('Invalid or unsupported LCS archive entry')
            self.files[key] = (at, size)

    def read(self, offset, size):
        if size > RESOURCE_LIMIT or offset > self.length or size > self.length - offset:
            raise ValueError('LCS resource is too large or outside its archive')
        self.stream.seek(offset)
        raw = bytearray(self.stream.read(size))
        if len(raw) != size:
            raise ValueError('Truncated LCS resource')
        for phase, key in ((0, 0xaf), (1, 0x66)):
            index = (phase - offset) & 1
            raw[index::2] = bytes(raw[index::2]).translate(bytes(b ^ key for b in range(256)))
        return bytes(raw)

    def get(self, name):
        row = self.files.get(zlib.crc32(name.lower().encode('ascii')) ^ 0xffffffff)
        if row is None:
            raise ValueError(f'LCS APK is missing {name}')
        return self.read(*row)


def open_lcs(apk):
    archive = zipfile.ZipFile(apk)
    try:
        if archive.namelist().count('AndroidManifest.xml') != 1 or archive.namelist().count('assets/data_main.wad') != 1:
            raise ValueError('LCS APK needs one manifest and a full data_main.wad')
        manifest = archive.getinfo('AndroidManifest.xml')
        if manifest.file_size > 1 << 20:
            raise ValueError('LCS manifest is too large')
        package, version = manifest_info(archive.read(manifest))
        if (package, version) != (PACKAGE, VERSION):
            raise ValueError(f'This APK is {package} {version}. Enhancements needs Liberty City Stories for Android {VERSION}.')
        info = archive.getinfo('assets/data_main.wad')
        # ZIP_STORED permits bounded random reads without inflating a 1.5GB archive.
        if info.compress_type != zipfile.ZIP_STORED:
            raise ValueError('LCS data_main.wad must be stored without ZIP compression')
        return archive, Wad(archive.open(info), info.file_size)
    except BaseException:
        archive.close()
        raise


def characters(wad):
    peds, inside = {}, False
    for line in wad.get('data/default.ide').decode('ascii').splitlines():
        line = line.split('#', 1)[0].strip().lower()
        if line == 'peds':
            inside = True
        elif line == 'end':
            inside = False
        elif inside and ',' in line:
            fields = [s.strip() for s in line.split(',')]
            model, txd = fields[1:3]
            if model != 'null':
                peds[safe_name(model)] = safe_name(txd)
    peds['player'] = 'player'
    return peds


def inspect(apk):
    archive, wad = open_lcs(apk)
    try:
        characters(wad)
        return {'version': VERSION, 'characters': len(WANTED)}
    finally:
        archive.close()


def model_textures(data):
    """Material texture names from mobile RSL clumps (geometry is a packed block)."""
    names = set()
    def chunk(at, end):
        if at + 12 > end:
            raise ValueError('Truncated LCS model chunk')
        kind, size = struct.unpack_from('<II', data, at)
        if size > end - at - 12:
            raise ValueError('Invalid LCS model chunk')
        return kind, at + 12, at + 12 + size
    _, begin, end = chunk(0, len(data))
    at = begin
    while at < end:
        kind, begin, next_at = chunk(at, end)
        if kind == 20:
            _, _, p = chunk(begin, next_at)
            kind, geometry, _ = chunk(p, next_at)
            if kind != 15:
                raise ValueError('Missing LCS geometry')
            size = struct.unpack_from('<I', data, geometry + 16)[0] & 0xfffff
            kind, materials, materials_end = chunk(geometry + size, next_at)
            if kind != 8:
                raise ValueError('Missing LCS materials')
            def scan(p, end, texture=False):
                while p < end:
                    kind, payload, next_p = chunk(p, end)
                    if kind == 2 and texture:
                        name = data[payload:next_p].split(b'\0')[0].decode('ascii').lower()
                        if name:
                            names.add(safe_name(name))
                        return
                    if kind in (7, 6):
                        scan(payload, next_p, kind == 6)
                    p = next_p
            scan(materials, materials_end)
        at = next_at
    return names


def extract(apk, destination, progress=None):
    archive, wad = open_lcs(apk)
    try:
        peds = {model: txd for model, txd in characters(wad).items() if model in WANTED}
        directory = wad.get('models/gta3.dir')
        if len(directory) % 32:
            raise ValueError('Invalid LCS IMG directory')
        img_at, img_size = wad.files[zlib.crc32(b'models/gta3.img') ^ 0xffffffff]
        entries = {}
        for at in range(0, len(directory), 32):
            sector, sectors = struct.unpack_from('<II', directory, at)
            name = directory[at + 8:at + 32].split(b'\0')[0].decode('ascii').lower()
            offset, size = sector * 2048, sectors * 2048
            if offset > img_size or size > img_size - offset or name in entries:
                raise ValueError('Invalid or duplicate LCS IMG entry')
            entries[name] = (img_at + offset, size)
        bank = wad.get('anim/ped.ifp')
        for name in BIKE_BANKS:
            if name not in entries:
                raise ValueError(f'LCS APK is missing {name}')
            bike = wad.read(*entries[name])
            if bike[:4] != b'ANPK' or len(bike) < 8:
                raise ValueError('Invalid LCS bike animation bank')
            end = 8 + ((struct.unpack_from('<I', bike, 4)[0] + 3) & ~3)
            if end > len(bike):
                raise ValueError('Truncated LCS bike animations')
            bank += bike[:end]
        resources = {'ped.ifp': bank}
        loaded = {}
        for model, txd in sorted(peds.items()):
            if model + '.dff' not in entries:
                continue  # reserved special character slots have no model
            resources[f'models/{model}.dff'] = wad.read(*entries[model + '.dff'])
            loaded[model] = txd
        if loaded.keys() != WANTED:
            raise ValueError('LCS APK is missing required character models')
        mappings = {}
        fallback = {}
        for path, txd, texture, *_ in csv.reader(io.StringIO(wad.get('master_texture_mapping_process.csv').decode('ascii'))):
            texture, txd = texture.lower(), txd.lower()
            mappings[(txd, texture)] = path
            fallback.setdefault(texture, path)
        for model, txd in loaded.items():
          for texture in model_textures(resources[f'models/{model}.dff']):
            path = mappings.get((txd, texture), fallback.get(texture))
            if path is None:
                raise ValueError(f'LCS APK is missing texture {texture}')
            parts = path.lower().split('/')
            if len(parts) != 2:
                raise ValueError('Invalid LCS texture path')
            for part in parts:
                safe_name(part)
            safe_name(texture.lower())
            resources[f'textures/{txd}/{texture}.pvr'] = wad.get(f'textures_etc/{path}.pvr')
        resources['characters.txt'] = ''.join(f'{model},{txd}\n' for model, txd in sorted(loaded.items())).encode()
        resources['source.txt'] = f'{PACKAGE}\n{VERSION}\n'.encode()
        total = sum(len(data) for data in resources.values())
        if total > LIMIT:
            raise ValueError('LCS character pack exceeds the import limit')
        destination = Path(destination)
        for index, (name, data) in enumerate(sorted(resources.items()), 1):
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            if progress:
                progress(index, len(resources))
        return len(loaded)
    finally:
        archive.close()
