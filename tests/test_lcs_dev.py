# SPDX-License-Identifier: GPL-3.0-only
import tempfile
import unittest
from pathlib import Path

from lcs_fixture import apk
from import_lcs_dev import prepare, STAMP


class LcsDevelopmentTests(unittest.TestCase):
    def test_incremental_import_and_missing_asset_recovery(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            source, output = root / 'lcs.apk', root / 'mods/Enhancements/lcs'
            apk(source)
            self.assertTrue(prepare(source, output))
            stamp = (output / STAMP).stat().st_mtime_ns
            self.assertFalse(prepare(source, output))
            self.assertEqual((output / STAMP).stat().st_mtime_ns, stamp)
            (output / 'textures/player/test.pvr').unlink()
            self.assertTrue(prepare(source, output))
            self.assertTrue((output / 'textures/player/test.pvr').is_file())

    def test_failed_donor_preserves_previous_import_and_user_settings(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            source, output = root / 'lcs.apk', root / 'mods/Enhancements/lcs'
            apk(source)
            prepare(source, output)
            before = (output / STAMP).read_bytes()
            settings = output.parent / 'settings.ini'
            settings.write_text('third_person.enabled=1\n')
            apk(source, version='2.4.380')
            with self.assertRaises(ValueError):
                prepare(source, output)
            self.assertEqual((output / STAMP).read_bytes(), before)
            self.assertEqual(settings.read_text(), 'third_person.enabled=1\n')
            self.assertFalse(list(output.parent.glob('.ctw-lcs-*')))
