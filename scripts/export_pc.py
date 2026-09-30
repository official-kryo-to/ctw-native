# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kryo.to
# See LICENSE in the repository root.
"""Export your own GTA: Chinatown Wars Android APK as a standalone Windows PC game.

    python scripts/export_pc.py [--apk PATH] [--out DIR] [--with-modkit]

The result is a clean folder with just the game:

    GTACTW.exe      the game
    data/           the game files from your APK (plain files: easy to look at and to mod)
    mods/           empty; put mods here (ModMenu.dll from the mod kit adds the F4 menu)

--with-modkit also puts the mod menu and the example mods into mods/. Needs the Windows build tools from
README.md; the game is built in release mode first if it is not built yet.
"""
import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from setup_game import prepare  # noqa: E402

RELEASE_BUILD = ROOT / 'port' / 'build-release'
NOT_EXPORTED = {'gtactw.ico'}   # goes into the exe instead
NOT_MODS = {'log.txt', 'enabled.ini'}


def run(command):
    print('>', ' '.join(map(str, command)), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def build(build_dir):
    run(['cmake', '-S', str(ROOT), '-B', str(build_dir), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
         '-DCTW_RELEASE=ON', '-DCTW_BUILD_MODKIT=ON', '-DBUILD_TESTING=OFF'])
    run(['cmake', '--build', str(build_dir)])


def set_exe_icon(exe, ico):
    """Put an .ico file's images into the exe's resources (Windows)."""
    import ctypes
    from ctypes import wintypes
    count = struct.unpack_from('<H', ico, 4)[0]
    entries = [struct.unpack_from('<BBBBHHII', ico, 6 + 16 * i) for i in range(count)]
    kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel32.BeginUpdateResourceW.restype = wintypes.HANDLE
    kernel32.BeginUpdateResourceW.argtypes = [wintypes.LPCWSTR, wintypes.BOOL]
    kernel32.UpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.LPVOID, wintypes.WORD,
                                         wintypes.LPVOID, wintypes.DWORD]
    kernel32.EndUpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.BOOL]
    handle = kernel32.BeginUpdateResourceW(str(exe), False)
    if not handle:
        raise OSError(ctypes.get_last_error(), 'BeginUpdateResource failed')
    group = struct.pack('<HHH', 0, 1, count)
    ok = True
    for index, (w, h, colors, _, planes, bits, size, offset) in enumerate(entries, 1):
        data = ico[offset:offset + size]
        buffer = ctypes.create_string_buffer(data, len(data))
        ok &= bool(kernel32.UpdateResourceW(handle, 3, index, 0x409, buffer, len(data)))   # RT_ICON
        group += struct.pack('<BBBBHHIH', w, h, colors, 0, planes, bits, size, index)
    buffer = ctypes.create_string_buffer(group, len(group))
    ok &= bool(kernel32.UpdateResourceW(handle, 14, 1, 0x409, buffer, len(group)))       # RT_GROUP_ICON
    if not kernel32.EndUpdateResourceW(handle, not ok):
        raise OSError(ctypes.get_last_error(), 'EndUpdateResource failed')
    if not ok:
        raise OSError('could not write the icon resources')


def export(apk, out, build_dir, rebuild, with_modkit):
    if out.exists():
        raise ValueError(f'{out} already exists. Choose another --out or remove it first.')
    if rebuild or not (build_dir / 'GTACTW.exe').is_file() or (with_modkit and not (build_dir / 'mods' / 'ModMenu.dll').is_file()):
        build(build_dir)
    with tempfile.TemporaryDirectory(prefix='ctw-export-') as temporary:
        stage = Path(temporary)
        package = stage / 'GTACTW'
        package.mkdir()
        prepare(apk, package / 'data')
        exe = package / 'GTACTW.exe'
        shutil.copyfile(build_dir / 'GTACTW.exe', exe)
        for name in NOT_EXPORTED:
            extra = package / 'data' / name
            if extra.is_file():
                if name.endswith('.ico'):
                    set_exe_icon(exe, extra.read_bytes())
                extra.unlink()
        if with_modkit:
            shutil.copytree(build_dir / 'mods', package / 'mods', ignore=lambda folder, names: [n for n in names if n in NOT_MODS])
        else:
            (package / 'mods').mkdir()
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(package), str(out))
    print(f'Exported the game to {out}')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--apk', type=Path, help='Your legally obtained Android 4.4.243 APK (default: the one in input/)')
    parser.add_argument('--out', type=Path, default=ROOT / 'dist' / 'GTA-Chinatown-Wars-PC', help='New output folder')
    parser.add_argument('--build-dir', type=Path, default=RELEASE_BUILD, help='CMake release build folder')
    parser.add_argument('--rebuild', action='store_true', help='Build the game again before exporting')
    parser.add_argument('--with-modkit', action='store_true', help='Also put the mod menu and the example mods into mods/')
    args = parser.parse_args()
    apk = args.apk
    if apk is None:
        candidates = sorted((ROOT / 'input').glob('*.apk'))
        if len(candidates) != 1:
            parser.error('Place exactly one APK in input/ or supply --apk PATH.')
        apk = candidates[0]
    try:
        export(apk.resolve(), args.out.resolve(), args.build_dir.resolve(), args.rebuild, args.with_modkit)
    except (OSError, ValueError, KeyError, zipfile.BadZipFile, subprocess.CalledProcessError) as exc:
        parser.exit(1, f'Export failed: {exc}\n')


if __name__ == '__main__':
    main()
