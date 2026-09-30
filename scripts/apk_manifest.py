# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kryo.to
# See LICENSE in the repository root.
"""Read package/version metadata from Android binary XML without running an APK."""
import struct
import xml.etree.ElementTree as ET


class ManifestError(ValueError):
    pass


def manifest_info(data):
    if data.lstrip().startswith(b'<'):
        try:
            root = ET.fromstring(data)
        except ET.ParseError as exc:
            raise ManifestError('Malformed Android manifest.') from exc
        ns = '{http://schemas.android.com/apk/res/android}'
        return root.get('package'), root.get(ns + 'versionName')
    try:
        kind, header, total = struct.unpack_from('<HHI', data)
        if kind != 3 or header < 8 or total != len(data):
            raise ManifestError('Invalid Android manifest header.')
        strings = []
        offset = header
        while offset < total:
            kind, header_size, size = struct.unpack_from('<HHI', data, offset)
            end = offset + size
            if header_size < 8 or size < header_size or end > total:
                raise ManifestError('Invalid Android manifest chunk.')
            chunk = data[offset:end]
            if kind == 1:  # ResStringPool
                count, _, flags, start, _ = struct.unpack_from('<5I', chunk, 8)
                if header_size < 28 or header_size + count * 4 > size:
                    raise ManifestError('Invalid manifest string pool.')

                def length(pos, utf8):
                    fmt, step, mask = ('<B', 1, 0x80) if utf8 else ('<H', 2, 0x8000)
                    value = struct.unpack_from(fmt, chunk, pos)[0]
                    pos += step
                    if value & mask:
                        value = ((value & (mask - 1)) << (step * 8)) | struct.unpack_from(fmt, chunk, pos)[0]
                        pos += step
                    return value, pos

                strings = []
                for i in range(count):
                    pos = start + struct.unpack_from('<I', chunk, header_size + i * 4)[0]
                    utf8 = bool(flags & 0x100)
                    n, pos = length(pos, utf8)
                    if utf8:
                        n, pos = length(pos, True)
                    n *= 1 if utf8 else 2
                    if pos + n > size:
                        raise ManifestError('Truncated manifest string.')
                    strings.append(chunk[pos:pos + n].decode('utf-8' if utf8 else 'utf-16le'))
            elif kind == 0x102:  # ResXMLStartElement
                _, name, attr_start, attr_size, count = struct.unpack_from('<IIHHH', chunk, header_size)
                if strings[name] == 'manifest':
                    if attr_size < 20:
                        raise ManifestError('Invalid manifest attributes.')
                    values = {}
                    for i in range(count):
                        pos = header_size + attr_start + i * attr_size
                        if pos + 20 > size:
                            raise ManifestError('Truncated manifest attribute.')
                        ns, key, raw, _, _, value_type, value = struct.unpack_from('<IIIHBBI', chunk, pos)
                        if raw != 0xFFFFFFFF:
                            decoded = strings[raw]
                        elif value_type == 3:
                            decoded = strings[value]
                        else:
                            decoded = str(value)
                        namespace = None if ns == 0xFFFFFFFF else strings[ns]
                        values[namespace, strings[key]] = decoded
                    return (values.get((None, 'package')),
                            values.get(('http://schemas.android.com/apk/res/android', 'versionName')))
            offset = end
    except (struct.error, IndexError, UnicodeError, ET.ParseError) as exc:
        raise ManifestError('Malformed Android manifest.') from exc
    raise ManifestError('No manifest element found.')
