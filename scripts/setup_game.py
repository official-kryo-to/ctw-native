# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kryo.to
# See LICENSE in the repository root.
"""Prepare local game data from a user-supplied APK. Never run Android code."""
import argparse
import io
from pathlib import Path, PurePosixPath
import shutil
import stat
import tempfile
import zipfile

from apk_manifest import ManifestError, manifest_info
from extract_tables import TableError, extract_tables

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = 'com.rockstargames.gtactw'
VERSION = '4.4.243'
REQUIRED = {'game.pak', 'rom.wad', 'rom.toc'}


def asset_entries(archive):
    entries, seen = [], set()
    for entry in archive.infolist():
        if entry.orig_filename.startswith('assets/') and entry.orig_filename != entry.filename:
            raise ValueError(f'Unsafe APK asset path: {entry.orig_filename}')
        if not entry.filename.startswith('assets/') or entry.is_dir():
            continue
        name = entry.filename[len('assets/'):]
        path = PurePosixPath(name)
        # Reject traversal, Windows drive/ADS syntax, aliases and Unix symlinks.
        if (not name or path.is_absolute() or '\\' in name or ':' in name or
                any(p in ('', '.', '..') or p.endswith((' ', '.')) for p in name.split('/')) or
                stat.S_ISLNK(entry.external_attr >> 16)):
            raise ValueError(f'Unsafe APK asset path: {entry.filename}')
        key = name.casefold()
        if key in seen:
            raise ValueError(f'Duplicate APK asset path: {entry.filename}')
        seen.add(key)
        entries.append((entry, path))
    if not REQUIRED <= seen:
        raise ValueError('APK is missing required assets: ' + ', '.join(sorted(REQUIRED - seen)))
    return entries


def launcher_icon(archive):
    """The game's launcher icon as .ico bytes (used for GTACTW.exe), or None without Pillow / an icon."""
    try:
        from PIL import Image
    except ImportError:
        return None
    for name in ('res/mipmap-xxxhdpi-v4/ic_launcher.png', 'res/mipmap-xxhdpi-v4/ic_launcher.png'):
        try:
            image = Image.open(io.BytesIO(archive.read(name))).convert('RGBA')
        except (KeyError, OSError):
            continue
        output = io.BytesIO()
        image.resize((256, 256), Image.LANCZOS).save(output, format='ICO',
                                                     sizes=[(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (16, 16)])
        return output.getvalue()
    return None


def prepare(apk, destination):
    if destination.exists():
        raise ValueError(f'{destination} already exists. Choose a new --data directory or move it aside first.')
    with zipfile.ZipFile(apk) as archive:
        package, version = manifest_info(archive.read('AndroidManifest.xml'))
        if package != PACKAGE or version != VERSION:
            raise ValueError(f'Expected {PACKAGE} Android {VERSION}; found {package} {version}.')
        entries = asset_entries(archive)
        print(f'Validated {package} {version}; reading ARM64 lookup tables.', flush=True)
        tables = extract_tables(archive.read('lib/arm64-v8a/libGame.so'))
        destination.parent.mkdir(parents=True, exist_ok=True)
        # Publish only a complete extraction; an interrupted run leaves no partial data directory.
        with tempfile.TemporaryDirectory(prefix='.ctw-setup-', dir=destination.parent) as temporary:
            stage = Path(temporary) / 'data'
            stage.mkdir()
            for index, (entry, path) in enumerate(entries, 1):
                target = stage.joinpath(*path.parts)
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(entry) as source, target.open('wb') as output:
                    shutil.copyfileobj(source, output, 1 << 20)
                if index % 250 == 0:
                    print(f'Extracted {index}/{len(entries)} local assets.', flush=True)
            for name, data in tables.items():
                (stage / name).write_bytes(data)
            icon = launcher_icon(archive)
            if icon:
                (stage / 'gtactw.ico').write_bytes(icon)   # picked up by CMake for development builds
            stage.rename(destination)
    print(f'Prepared {len(entries)} assets and {len(tables)} tables in {destination}.', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apk', type=Path, help='Your legally obtained Android 4.4.243 APK')
    parser.add_argument('--data', type=Path, default=ROOT / 'port/data', help='New local output directory')
    args = parser.parse_args()
    apk = args.apk
    if apk is None:
        candidates = sorted((ROOT / 'input').glob('*.apk'))
        if len(candidates) != 1:
            parser.error('Place exactly one APK in input/ or supply --apk PATH.')
        apk = candidates[0]
    try:
        prepare(apk.resolve(), args.data.resolve())
    except (OSError, ValueError, KeyError, zipfile.BadZipFile, ManifestError, TableError) as exc:
        parser.exit(1, f'Setup failed: {exc}\n')


if __name__ == '__main__':
    main()
