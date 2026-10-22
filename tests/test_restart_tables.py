# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kryo.to
# See LICENSE in the repository root.
"""Restart extraction from synthetic constant calls, without original coordinates."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from extract_tables import GameBinary, TableError


class RestartTableTests(unittest.TestCase):
    def fixture(self):
        game = GameBinary.__new__(GameBinary)
        calls = []
        for kind, count, point in [('Hospital', 5, (-4096, 8192, 123, -90)),
                                   ('Police', 12, (12288, -4096, 456, 180))]:
            x, y, z, heading = point
            packed = struct.unpack('<Q', struct.pack('<ii', x, y))[0]
            for _ in range(count):
                calls.append((f'Add{kind}RestartPoint', {'x1': 100, 'x2': heading & 0xffffffff},
                              {100: packed, 108: z}))
        game.constant_calls = lambda name: calls
        return game, calls

    def test_points_remain_in_separate_groups_with_signed_values(self):
        game, calls = self.fixture()
        calls.reverse()  # output groups do not depend on interleaving of the native calls
        self.assertEqual(game.restart(), b'CTWRESP2' + struct.pack('<II', 5, 12) +
                         struct.pack('<4i', -4096, 8192, 123, -90) * 5 +
                         struct.pack('<4i', 12288, -4096, 456, 180) * 12)

    def test_requires_both_complete_groups(self):
        for missing in (0, 16):
            game, calls = self.fixture()
            calls.pop(missing)
            with self.assertRaises(TableError):
                game.restart()

    def test_unresolved_arguments_fail_in_each_group(self):
        for index in (0, 6):
            for kind, key in ((1, 'x1'), (1, 'x2'), (2, 100), (2, 108)):
                game, calls = self.fixture()
                del calls[index][kind][key]
                with self.assertRaises(TableError):
                    game.restart()


if __name__ == '__main__':
    unittest.main()
