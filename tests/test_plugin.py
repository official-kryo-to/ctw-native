# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kryo.to
# See LICENSE in the repository root.
"""An incompatible host must be rejected before any callback is dereferenced."""
import ctypes
from pathlib import Path
import unittest

DLL = Path(__file__).resolve().parents[1] / 'port/build/mods/ModMenu.dll'


@unittest.skipUnless(DLL.is_file(), 'Build the mod kit before running ABI checks')
class HostAbiTests(unittest.TestCase):
    def test_incomplete_host_table_is_rejected(self):
        class Header(ctypes.Structure):
            _fields_ = [('version', ctypes.c_uint32), ('size', ctypes.c_uint32)]

        library = ctypes.CDLL(str(DLL))
        init = library.ctw_plugin_init
        init.argtypes = [ctypes.c_void_p]
        init.restype = ctypes.c_int
        self.assertEqual(init(None), 1)
        for version, size in ((0, 8), (1, 8), (3, 8), (99, 8)):
            with self.subTest(version=version):
                self.assertEqual(init(ctypes.byref(Header(version, size))), 1)


if __name__ == '__main__':
    unittest.main()
