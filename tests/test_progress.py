# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kryo.to
# See LICENSE in the repository root.
"""The progress tooling on a synthetic reference and source tree; no game data."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import progress  # noqa: E402


def reference(*classes):
    subs = {}
    for sub, name, routines in classes:
        key = (lambda m: f'{name}::{m}') if name != progress.FREE else (lambda m: f'::{m}')
        entry = {'name': name, 'hashes': ''.join(progress.name_hash(key(m)) for m, _ in routines),
                 'sizes': [s for _, s in routines]}
        subs.setdefault(sub, []).append(entry)
    return {'build': 'synthetic', 'subsystems': [{'name': n, 'classes': c} for n, c in subs.items()]}


class ProgressTest(unittest.TestCase):
    def test_demangle(self):
        self.assertEqual(progress.demangle('_ZN14cMuzzleFlashes3AddER4tv3dI6cFixedILj20ELj12EEEiaS1_ILj4ELj12EE'),
                         ('cMuzzleFlashes', 'Add'))
        self.assertEqual(progress.demangle('_ZNK4cPed13FireDirectionEv'), ('cPed', 'FireDirection'))
        self.assertEqual(progress.demangle('_ZN6cTazerC2Ev'), ('cTazer', 'cTazer'))
        self.assertEqual(progress.demangle('_ZN6cTazerD0Ev'), ('cTazer', '~cTazer'))
        self.assertEqual(progress.demangle('_Z17Rand32NonCriticalj'), ('', 'Rand32NonCritical'))
        self.assertEqual(progress.demangle('_ZN3Gui7cHudApp7ProcessEv'), ('cHudApp', 'Process'))
        self.assertIsNone(progress.demangle('memcpy'))

    def test_measure_counts_named_routines_by_size(self):
        ref = reference(('Weapons', 'cTazer', [('Fire', 100), ('Update', 300)]),
                        ('Effects', progress.FREE, [('fastsin', 50)]))
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp) / 'port' / 'game'
            game.mkdir(parents=True)
            (game / 'tazer.cpp').write_text('// cTazer::Update and fastsin, but cTazer only called Fire\n')
            result = progress.measure(Path(tmp), ref)
        self.assertEqual((result['portedRoutines'], result['routines']), (2, 3))
        self.assertEqual((result['portedBytes'], result['bytes']), (350, 450))
        weapons = next(s for s in result['subsystems'] if s['name'] == 'Weapons')
        self.assertEqual(weapons['classes'][0]['portedBytes'], 300)

    def test_outputs_and_compare(self):
        ref = reference(('Weapons', 'cTazer', [('Fire', 100), ('Update', 300)]))
        with tempfile.TemporaryDirectory() as tmp:
            root, out = Path(tmp) / 'src', Path(tmp) / 'out'
            (root / 'port' / 'game').mkdir(parents=True)
            src = root / 'port' / 'game' / 't.cpp'
            src.write_text('// nothing yet\n')
            progress.write(progress.measure(root, ref), out / 'base')
            src.write_text('// cTazer::Fire\n')
            progress.write(progress.measure(root, ref), out / 'head')
            for name in ('progress.json', 'progress.svg', 'progress.html', 'badge-overall.svg', 'badge-routines.svg'):
                self.assertTrue((out / 'head' / name).stat().st_size > 0, name)
            self.assertIn('25.00%', (out / 'head' / 'badge-overall.svg').read_text())
            report = progress.compare(out / 'base' / 'progress.json', out / 'head' / 'progress.json')
            self.assertIn('0.00% → 25.00%', report)
            self.assertIn('`cTazer` 0 → 1', report)
            self.assertEqual(progress.compare(out / 'head' / 'progress.json', out / 'head' / 'progress.json'), '')

    def test_squarify_fills_the_area(self):
        boxes = progress.squarify([6, 6, 4, 3, 2, 2, 1], 0, 0, 600, 400)
        self.assertAlmostEqual(sum(w * h for _, _, w, h in boxes), 600 * 400, delta=1)
        for x, y, w, h in boxes:
            self.assertGreaterEqual(x, -1e-6)
            self.assertLessEqual(x + w, 600 + 1e-6)
            self.assertLessEqual(y + h, 400 + 1e-6)

    def test_committed_reference_is_well_formed(self):
        ref = json.loads(progress.REFERENCE.read_text(encoding='utf-8'))
        for sub in ref['subsystems']:
            for cls in sub['classes']:
                self.assertEqual(len(cls['hashes']), 8 * len(cls['sizes']), cls['name'])


if __name__ == '__main__':
    unittest.main()
