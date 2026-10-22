# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kryo.to
# See LICENSE in the repository root.
"""Navigation metadata extraction from synthetic constant-call evidence, never game lookup data."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from extract_tables import GameBinary, TableError


class NavigationTableTests(unittest.TestCase):
    def fixture(self):
        game = GameBinary.__new__(GameBinary)
        nav = ('_ZNK8ZoneImpl10SetNavZoneEPKcj10eAffluenceb',
               {'x1': 100, 'x2': 513, 'x3': 3, 'x4': 1}, {})
        island = ('_ZNK8ZoneImpl13SetIslandZoneEPKcj', {'x1': 200, 'x2': 2}, {})
        calls = [nav, island]
        game.constant_calls = lambda name: calls
        names = {100: b'EIGHTCHR\0', 200: b'TAILEND\0'}
        game.read = lambda address, count: names[address].ljust(count, b'\0')
        return game, calls, names

    def test_compact_records_preserve_text_affluence_bleed_and_island(self):
        game, calls, _ = self.fixture()
        calls.insert(1, calls[0])  # repeated setup names preserve native last-assignment semantics
        self.assertEqual(game.navigation(), b'CTWNAV1\0' + struct.pack('<II', 2, 1) +
                         struct.pack('<8sHBB', b'EIGHTCHR', 513, 3, 1) * 2 +
                         struct.pack('<8sI', b'TAILEND', 2))

    def test_rejects_unresolved_or_out_of_range_arguments(self):
        for register, value in [('x1', None), ('x2', -1), ('x2', 65536), ('x3', None),
                                ('x3', 4), ('x4', None), ('x4', 2)]:
            with self.subTest(register=register, value=value):
                game, calls, _ = self.fixture()
                calls[0][1][register] = value
                with self.assertRaises(TableError):
                    game.navigation()
        for value in (None, -1, 256):
            game, calls, _ = self.fixture()
            calls[1][1]['x2'] = value
            with self.assertRaises(TableError):
                game.navigation()

    def test_rejects_invalid_names_or_missing_definition_groups(self):
        for name in (b'', b'LONGERTHAN8'):
            game, _, names = self.fixture()
            names[100] = name + b'\0'
            with self.assertRaises(TableError):
                game.navigation()
        for missing in (0, 1):
            game, calls, _ = self.fixture()
            calls.pop(missing)
            with self.assertRaises(TableError):
                game.navigation()


if __name__ == '__main__':
    unittest.main()
