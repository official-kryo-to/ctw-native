# SPDX-License-Identifier: GPL-3.0-only
import tempfile
import unittest
from pathlib import Path
from lcs_fixture import apk
import lcs_assets


class LcsImportTests(unittest.TestCase):
    def test_selected_resources_and_bytes(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);source=root/'lcs.apk';apk(source)
            self.assertEqual(lcs_assets.inspect(source)['version'],'2.4.379')
            self.assertEqual(lcs_assets.extract(source,root/'out'),14)
            expected=b'synthetic animation'+b''.join(b'ANPK\x04\x00\x00\x00'+bytes((i,))*4 for i in range(4))
            self.assertEqual((root/'out/ped.ifp').read_bytes(),expected)
            self.assertEqual(len(list((root/'out/models').glob('*.dff'))),14)
            self.assertEqual((root/'out/textures/player/test.pvr').read_bytes(),b'synthetic texture')
            self.assertFalse((root/'out/data_main.wad').exists())

    def test_wrong_game_version_and_archive_bounds(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            for options in ({'version':'2.4.380'}, {'package':'other.app'}, {'bad_offset':True}):
                source=root/'bad.apk';apk(source,**options)
                with self.assertRaises(ValueError):lcs_assets.extract(source,root/'out')
                self.assertFalse((root/'out').exists())

    def test_resource_paths_and_truncated_models(self):
        for name in ('../x','a/b','c:asset','x.','CON','con','nul','com1','lpt9',''):
            with self.assertRaises(ValueError):lcs_assets.safe_name(name)
        with self.assertRaises(ValueError):lcs_assets.model_textures(b'\0'*11)

    def test_missing_or_truncated_style_bank(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            for options in ({'bike_banks':('bikev.ifp',)}, {'bad_bike':True}):
                source=root/'bad.apk';apk(source,**options)
                with self.assertRaises(ValueError):lcs_assets.extract(source,root/'out')
                self.assertFalse((root/'out').exists())
