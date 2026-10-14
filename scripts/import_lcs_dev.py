# SPDX-License-Identifier: GPL-3.0-only
"""Prepare optional private LCS character assets for a development mod build."""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile

import apk_manifest
import lcs_assets

STAMP = '.ctw-import.json'


def signature(apk):
    stat = apk.stat()
    tools = hashlib.sha256()
    for name in (__file__, lcs_assets.__file__, apk_manifest.__file__):
        tools.update(Path(name).read_bytes())
    return {'apk': str(apk), 'size': stat.st_size, 'mtime_ns': stat.st_mtime_ns,
            'tools': tools.hexdigest()}


def current(destination, source):
    try:
        stamp = json.loads((destination / STAMP).read_text())
        files = stamp['files']
        required = {f'models/{name}.dff' for name in lcs_assets.WANTED}
        required.update(('ped.ifp', 'characters.txt', 'source.txt'))
        if stamp['source'] != source or not required.issubset(files):
            return False
        for name, size in files.items():
            path = destination / name
            if path.is_symlink() or not path.resolve().is_relative_to(destination):
                return False
            if not path.is_file() or path.stat().st_size != size:
                return False
        return True
    except (OSError, ValueError, KeyError, TypeError, AttributeError):
        return False


def prepare(apk, destination):
    apk = Path(apk).resolve(strict=True)
    destination = Path(destination)
    if destination.is_symlink() or (destination.exists() and not destination.is_dir()):
        raise ValueError('The LCS output must be a regular directory')
    destination = destination.resolve()
    source = signature(apk)
    if current(destination, source):
        return False
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Both folders stay in this output directory, so publication is a rename.
    # A bad donor never replaces a previous successful import.
    with tempfile.TemporaryDirectory(prefix='.ctw-lcs-', dir=destination.parent) as temporary:
        stage = Path(temporary) / 'lcs'
        count = lcs_assets.extract(apk, stage)
        files = {p.relative_to(stage).as_posix(): p.stat().st_size
                 for p in stage.rglob('*') if p.is_file()}
        (stage / STAMP).write_text(json.dumps({'source': source, 'files': files}, sort_keys=True))
        previous = Path(temporary) / 'previous'
        if destination.exists():
            destination.rename(previous)
        try:
            stage.rename(destination)
        except BaseException:
            if previous.exists() and not destination.exists():
                previous.rename(destination)
            raise
    print(f'Prepared {count} LCS {lcs_assets.VERSION} characters in {destination}.', flush=True)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apk', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    try:
        prepare(args.apk, args.out)
    except Exception as exc:
        parser.exit(1, f'LCS development import failed: {exc}\n')


if __name__ == '__main__':
    main()
