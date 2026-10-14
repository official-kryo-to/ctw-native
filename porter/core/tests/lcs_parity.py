# SPDX-License-Identifier: GPL-3.0-only
"""Compare Python and Rust LCS imports, with synthetic data or an optional own APK."""
from pathlib import Path
import hashlib
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[3]
sys.path[:0]=[str(ROOT/'scripts'),str(ROOT/'tests')]
import lcs_assets
from lcs_fixture import apk

def files(root):
    return {p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob('*') if p.is_file()}

with tempfile.TemporaryDirectory() as d:
    root=Path(d);source=Path(sys.argv[1]) if len(sys.argv)>1 else root/'fixture.apk'
    if len(sys.argv)==1:apk(source)
    lcs_assets.extract(source,root/'python')
    command=['cargo','run','--quiet','--manifest-path',str(ROOT/'porter/core/Cargo.toml'),'--example','lcs','--',str(source),str(root/'rust')]
    subprocess.run(command,check=True,cwd=ROOT)
    assert files(root/'python')==files(root/'rust'), 'LCS importers differ'
    print(f'LCS parity: {len(files(root/"rust"))} matching files')
