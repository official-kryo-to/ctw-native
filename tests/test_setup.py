# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kryo.to
# See LICENSE in the repository root.
"""Synthetic APK fixtures: version validation and extraction boundaries."""
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from apk_manifest import ManifestError, manifest_info
from setup_game import asset_entries, prepare


def binary_manifest(utf8):
    strings = ['manifest', 'package', 'versionName', 'com.rockstargames.gtactw', '4.4.243',
               'http://schemas.android.com/apk/res/android']
    pool, offsets = bytearray(), []
    for string in strings:
        offsets.append(len(pool))
        encoded = string.encode('utf-8' if utf8 else 'utf-16le')
        pool += (bytes((len(string), len(encoded))) if utf8 else struct.pack('<H', len(string)))
        pool += encoded + (b'\0' if utf8 else b'\0\0')
    start = 28 + len(strings) * 4
    size = start + len(pool)
    pool_chunk = (struct.pack('<HHI5I', 1, 28, size, len(strings), 0, 0x100 if utf8 else 0, start, 0) +
                  struct.pack('<6I', *offsets) + pool)
    attrs = (struct.pack('<IIIHBBI', 0xFFFFFFFF, 1, 3, 8, 0, 3, 3) +
             struct.pack('<IIIHBBI', 5, 2, 0xFFFFFFFF, 8, 0, 3, 4))
    element = (struct.pack('<HHIII', 0x102, 16, 36 + len(attrs), 1, 0xFFFFFFFF) +
               struct.pack('<II6H', 0xFFFFFFFF, 0, 20, 20, 2, 0, 0, 0) + attrs)
    content = pool_chunk + element
    return struct.pack('<HHI', 3, 8, 8 + len(content)) + content


class SetupTests(unittest.TestCase):
    def apk(self, entries, manifest=None):
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, 'w') as archive:
            archive.writestr('AndroidManifest.xml', manifest or binary_manifest(True))
            for name, value in entries:
                entry = zipfile.ZipInfo(name)
                entry.filename = name  # preserve hostile backslashes even on Windows
                archive.writestr(entry, value)
        stream.seek(0)
        return stream

    def test_manifest_string_encodings_and_typed_value(self):
        for utf8 in (True, False):
            self.assertEqual(manifest_info(binary_manifest(utf8)), ('com.rockstargames.gtactw', '4.4.243'))

    def test_malformed_manifest(self):
        fixture = binary_manifest(True)
        for data in (b'bad', fixture[:-1], fixture[:16] + bytes(30)):
            with self.subTest(length=len(data)), self.assertRaises(ManifestError):
                manifest_info(data)

    def test_unsafe_asset_paths(self):
        for name in ('assets/../escape', 'assets//absolute', 'assets/C:/escape', 'assets/a\\b', 'assets/a./file'):
            with self.subTest(name=name), zipfile.ZipFile(self.apk([(name, b'')])) as archive:
                with self.assertRaisesRegex(ValueError, 'Unsafe'):
                    asset_entries(archive)

    def test_case_aliases_and_missing_assets(self):
        for entries, message in ([('assets/game.pak', b''), ('assets/GAME.PAK', b'')], 'Duplicate'), ([], 'missing'):
            with zipfile.ZipFile(self.apk(entries)) as archive:
                with self.assertRaisesRegex(ValueError, message):
                    asset_entries(archive)

    def test_success_excludes_native_libraries_and_writes_local_tables(self):
        entries = [(f'assets/{name}', b'synthetic') for name in ('game.pak', 'rom.wad', 'rom.toc')]
        entries += [('lib/arm64-v8a/libGame.so', b'not executable'), ('other/private.dat', b'not an asset')]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            apk = root / 'fixture.apk'; apk.write_bytes(self.apk(entries).getvalue())
            with patch('setup_game.extract_tables', return_value={'fixture_tables.bin': b'fixture'}):
                prepare(apk, root / 'data')
            self.assertEqual({p.name for p in (root / 'data').iterdir()},
                             {'game.pak', 'rom.wad', 'rom.toc', 'fixture_tables.bin'})

    def test_wrong_version_and_existing_data_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            apk = root / 'fixture.apk'
            manifest = b'<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.rockstargames.gtactw" android:versionName="1.0"/>'
            apk.write_bytes(self.apk([], manifest).getvalue())
            with patch('setup_game.extract_tables') as extraction:
                with self.assertRaisesRegex(ValueError, 'Expected'):
                    prepare(apk, root / 'data')
                extraction.assert_not_called()
            self.assertFalse((root / 'data').exists())
            (root / 'data').mkdir()
            sentinel = root / 'data' / 'keep'; sentinel.write_text('keep')
            with self.assertRaisesRegex(ValueError, 'already exists'):
                prepare(apk, root / 'data')
            self.assertEqual(sentinel.read_text(), 'keep')
