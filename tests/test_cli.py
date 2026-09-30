# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kryo.to
# See LICENSE in the repository root.
"""Argument errors must be reported before creating a window or loading assets."""
from pathlib import Path
import subprocess
import unittest


EXE = Path(__file__).resolve().parents[1] / 'port' / 'build' / 'GTACTW.exe'


@unittest.skipUnless(EXE.is_file(), 'Build the Windows game before running CLI checks')
class CommandLineTests(unittest.TestCase):
    def test_license_without_game_data_or_window(self):
        result = subprocess.run([str(EXE), '--license'], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('GNU GPL version 3 only', result.stdout)

    def reject(self, args, message):
        result = subprocess.run([str(EXE), *args], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 4, result.stdout + result.stderr)
        self.assertIn(message, result.stderr)

    def test_missing_option_value(self):
        self.reject(['--frames'], '--frames requires 1 value(s)')

    def test_incomplete_position(self):
        self.reject(['--pos', '-1000'], '--pos requires 2 value(s)')

    def test_unknown_option_after_flag(self):
        self.reject(['--trace', '--unknown'], 'Unknown option: --unknown')

    def test_option_values_are_not_reparsed(self):
        self.reject(['--mods', '--trace', '--unknown'], 'Unknown option: --unknown')


if __name__ == '__main__':
    unittest.main()
