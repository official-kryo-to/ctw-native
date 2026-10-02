# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kryo.to
# See LICENSE in the repository root.
"""The mod menu and the example mods' windows: no text on other text, nothing off screen, and nothing drawn over
the game's own clock and help line - at several window sizes and font heights.

Builds ModMenu and the example mods for the machine running the tests (with a GCC/Clang-style C++ compiler) and runs
them against tests/layout_host.cpp, a stand-in for the game that records what they draw.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
KIT = ROOT / 'ctw-modkit'
WINDOWS = sys.platform == 'win32'
CXX = shutil.which('c++') or shutil.which('g++') or shutil.which('clang++')
SIZES = [(1024, 600), (1280, 720), (1920, 1080)]
LINE_HEIGHTS = [16, 20]   # the game's font is 16; 20 checks that nothing depends on that exact number
F4, F7, F8 = 'key:61', 'key:64', 'key:65'
# Each scenario is a list of host actions: keys, and clicks on text the menu drew.
SCENARIOS = {
    'menu': [F4],
    'menu-expanded': [F4, 'click:Cheat Example', 'click:Broken Mod'],
    'menu-enhancements': [F4, 'click:Enhancements'],
    'menu-search': [F4, 'click:Search mods...'],
    'menu-spawner': [F4, 'click:Cheat Example', F7],
    'freecam': [F8],
    'spawner-freecam': [F7, F8],
}


def overlaps(a, b):
    return a[0] < b[0] + b[2] - 0.5 and b[0] < a[0] + a[2] - 0.5 and a[1] < b[1] + b[3] - 0.5 and b[1] < a[1] + a[3] - 0.5


def clipped(box, clip):
    if not clip:
        return box
    x0, y0 = max(box[0], clip[0]), max(box[1], clip[1])
    x1, y1 = min(box[0] + box[2], clip[0] + clip[2]), min(box[1] + box[3], clip[1] + clip[3])
    return None if x1 <= x0 or y1 <= y0 else (x0, y0, x1 - x0, y1 - y0)


def problems(frame):
    width, height = frame['W'], frame['H']
    clip, texts, panels, found = None, [], [], []
    for index, op in enumerate(frame['ops']):
        if op['t'] == 'clip':
            clip = (op['x'], op['y'], op['w'], op['h']) if op['w'] > 0 else None
        elif op['t'] == 'rect':
            box = clipped((op['x'], op['y'], op['w'], op['h']), clip)
            if box and op['w'] * op['h'] > 40000 and (op['c'] & 255) >= 0xC0:
                panels.append((index, box))
        elif op['t'] == 'text' and op['str'].strip():
            box = clipped((op['x'], op['y'], op['w'], op['h']), clip)
            if box:
                texts.append((index, box, op['str']))
    for n, (index, box, text) in enumerate(texts):
        if box[0] < 0 or box[1] < 0 or box[0] + box[2] > width + 0.5 or box[1] + box[3] > height + 0.5:
            found.append(f'off screen: {text!r}')
        found += [f'{text!r} on {other!r}' for _, other_box, other in texts[n + 1:] if overlaps(box, other_box)]
        found += [f'{text!r} under a window' for panel, panel_box in panels if panel > index and overlaps(box, panel_box)]
    return found


@unittest.skipUnless(CXX, 'needs a GCC/Clang-style C++ compiler')
class ModMenuLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = Path(tempfile.mkdtemp(prefix='ctw-layout-'))
        include = ['-I', str(KIT / 'include')]
        shared = ['-shared'] + (['-static'] if WINDOWS else ['-fPIC'])
        mods = cls.dir / 'mods'
        for folder in ('CheatExample', 'Enhancements', 'Trumpify', 'LongMod', 'Broken'):
            (mods / folder).mkdir(parents=True)
        cls.menu = cls.dir / ('ModMenu.dll' if WINDOWS else 'ModMenu.so')
        cls.host = cls.dir / ('host.exe' if WINDOWS else 'host')
        examples = KIT / 'examples'
        build = [
            [CXX, '-std=c++17', *shared, *include, str(KIT / 'modmenu/modmenu.cpp'), '-o', str(cls.menu)],
            [CXX, '-x', 'c', *shared, *include, str(examples / 'cheat_example/cheat_example.c'),
             '-o', str(mods / 'CheatExample/cheat_example.dll'), '-lm'],
            [CXX, '-x', 'c', *shared, *include, *map(str, sorted((examples / 'enhancements').glob('*.c'))),
             '-o', str(mods / 'Enhancements/enhancements.dll'), '-lm'],
            [CXX, '-std=c++17', *include, str(ROOT / 'tests/layout_host.cpp'), '-o', str(cls.host)]
            + ([] if WINDOWS else ['-ldl']),
        ]
        for command in build:
            subprocess.run(command, check=True)
        for example, folder in (('cheat_example', 'CheatExample'), ('enhancements', 'Enhancements'),
                                ('trumpify', 'Trumpify')):
            shutil.copy(examples / example / 'mod.ini', mods / folder)
        (mods / 'LongMod/mod.ini').write_text(
            'name = A Mod With A Really Quite Long Name For Testing\nauthor = someone\nversion = 12.3.4-beta\n'
            'description = This description is long on purpose, to check that the menu wraps it onto several lines '
            'instead of cutting it off or drawing it over the next row.\n')
        (mods / 'Broken/mod.ini').write_text('name = Broken Mod\nversion = 1.0\ndll = missing.dll\ndescription = No DLL.\n')

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.dir, ignore_errors=True)

    def test_nothing_overlaps(self):
        for width, height in SIZES:
            for line_height in LINE_HEIGHTS:
                for scenario, actions in SCENARIOS.items():
                    with self.subTest(size=f'{width}x{height}', font=line_height, scenario=scenario):
                        out = self.dir / 'frame.json'
                        subprocess.run([str(self.host), str(self.menu), f'{self.dir / "mods"}{os.sep}', str(width),
                                        str(height), str(line_height), str(out), *actions], check=True)
                        frame = json.loads(out.read_text())
                        self.assertGreater(len(frame['ops']), 3)
                        self.assertEqual(problems(frame), [])


if __name__ == '__main__':
    unittest.main()
