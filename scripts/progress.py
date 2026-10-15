# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kryo.to
# See LICENSE in the repository root.
"""Port progress: how much of the original game's code the port covers.

The reference is the list of routines in the supported Android 4.4.243 libGame.so. Its index,
progress/reference.json, holds class names, each routine's machine-code size and a short hash of its
`Class::Method` name: no code and no readable method names. A routine counts as ported once the port's source
names it as `Class::Method` (the comment convention of every ported routine). Progress is weighted by code size.

    python scripts/progress.py OUT_DIR                 progress.json, badges, progress.svg and progress.html
    python scripts/progress.py --index [--apk X.apk]    rebuild progress/reference.json from your own APK
    python scripts/progress.py --compare BASE HEAD      markdown summary of two progress.json files

CI runs the first form on every push to main and publishes the result to GitHub Pages; only the --index form
needs the game.
"""
import argparse
import hashlib
import html
import json
import math
import re
import sys
import zipfile
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / 'progress' / 'reference.json'
SOURCE_DIRS = ('port/src', 'port/game', 'port/showcase', 'ctw-modkit')
SOURCE_SUFFIXES = {'.cpp', '.h', '.c', '.hpp'}

# Library code linked into libGame.so that the PC build replaces wholesale (C++ runtime, codecs, Android glue).
LIBRARY = re.compile(r'^(std|__gnu_cxx|__cxxabiv1|__cxa|__gxx|_Unwind|Json|png|jpeg|ogg|vorbis|mpg123|z_|'
                     r'FMOD|fmod|android|ANDROID|JNI|Java|AAsset|AConfiguration|ALooper|ANativeActivity|'
                     r'cJNI|google|ndk|lzma|crc32|adler32|inflate|deflate|curl|ssl|SSL|EVP|BIO|X509|CRYPTO)')

# Subsystems by class-name pattern; the first match wins.
SUBSYSTEMS = [
    ('Missions', r'^c[A-Z]{3}_[A-Z]\d|tro\d*$|^cIntro|^cOutro|^cMidtro|Mission|Script|Objective|Cutscene|^cAct|'
                 r'^cEvent|Rampage|Kudos|Replay|Rival|Goon|^cAITeam|Buddy|SuperAI'),
    ('Minigames', r'App$|Range$|Ammozone|Tattoo|ScratchCard|HotWiring|BombDisposal|Garage|Taxi|AmbRace|Race|WHouse|'
                  r'Oddjob|OddJob|HQ$|Trade|Drug|Dealer|Stash|Shop|Courier'),
    ('Weapons', r'Weapon|Bullet|Muzzle|Projectile|Rocket|Grenade|Molotov|Mine|Sword|Chainsaw|Minigun|Pistol|Shotgun|'
                r'Uzi|Mp5|AK47|Colt|Taser|Tazer|Sniper|Flame|Targeting|SensorCone|Fist|NiteStick|Baseball|DesertEagle|'
                r'Bat$|Explosion|Damage'),
    ('People', r'^cPed|^cPlayer|Pedestrian|^cPopulation|Gang|^cCop|AITask|^cTask|Anim|Simple|Mover|Driver|Passenger'),
    ('Vehicles', r'Vehicle|^cCar|Bike|Boat|Heli|Tank|Wheel|Traffic|Road|Skid|Door|Train|JetSki|Plane'),
    ('Saves and wanted level', r'Trigger|Stat|Save|Progress|Award|Wanted|Police|Profile|Achievement|Unlock'),
    ('HUD and PDA', r'^Gui|Hud|Radar|Pda|PDA|Menu|Font|Text|Sprite|Button|Pad|Touch|Wnd|Screen|Map|Gps|GPS|Message|'
                    r'Help|Email|Frontend|Pause|Popup|Icon|Blip'),
    ('Effects', r'Particle|Effect|Emitter|Trail|Smoke|Fire|Spark|Debris|Light|Shadow|Water|Splash|Cannon|Weather|'
                r'Rain|immsprite'),
    ('Audio', r'Audio|Sound|Sfx|Radio|Music|Speech|Voice'),
    ('World and engine', r'World|Sector|Collision|Physics|Physical|Entity|Object|Prop|Building|Zone|Camera|Cam|'
                         r'TimeCycle|Clock|Matrix|Model|Mesh|Render|Gl|Texture|Asset|Res|Streaming|Pool|Memory|Area'),
]
OTHER = 'Other'
FREE = '(free functions)'


def name_hash(name):
    return hashlib.sha1(name.encode()).hexdigest()[:8]


# ---------------------------------------------------------------------------------------------- the index

def source_name(mangled, i):
    j = i
    while j < len(mangled) and mangled[j].isdigit():
        j += 1
    if j == i:
        return None, i
    n = int(mangled[i:j])
    return mangled[j:j + n], j + n


def demangle(mangled):
    """(class, method) for _ZN..E nested names, ('', name) for plain functions, None otherwise."""
    if not mangled.startswith('_Z'):
        return None
    if not mangled.startswith('_ZN'):
        name, _ = source_name(mangled, 2)
        return ('', name) if name else None
    i = 3
    while i < len(mangled) and mangled[i] in 'KVr':
        i += 1
    parts = []
    while i < len(mangled) and mangled[i] != 'E':
        c = mangled[i]
        if c.isdigit():
            name, i = source_name(mangled, i)
            if name is None:
                return None
            parts.append(name)
        elif c == 'I':   # template arguments
            depth = 0
            while i < len(mangled):
                depth += {'I': 1, 'E': -1}.get(mangled[i], 0)
                i += 1
                if depth == 0:
                    break
        elif c in 'CD' and i + 1 < len(mangled) and mangled[i + 1] in '0123':
            parts.append(('~' if c == 'D' else '') + (parts[-1] if parts else '?'))
            i += 2
        elif c == 'S':
            if mangled[i + 1:i + 2] == 't':
                i += 2
            else:
                k = mangled.find('_', i)
                if k < 0:
                    return None
                i = k + 1
        elif c == 'L':
            i += 1
        else:
            return None
    if not parts:
        return None
    if len(parts) == 1:
        return '', parts[0]
    return parts[-2], parts[-1]


def subsystem(cls, method):
    key = cls or method
    for title, pattern in SUBSYSTEMS:
        if re.search(pattern, key):
            return title
    return OTHER


def build_index(apk):
    sys.path.insert(0, str(ROOT / 'scripts'))
    from extract_tables import GameBinary
    if apk is None:
        found = sorted((ROOT / 'input').glob('*.apk'))
        if not found:
            sys.exit('Put your Android 4.4.243 APK in input/ or pass --apk.')
        apk = found[0]
    with zipfile.ZipFile(apk) as archive:
        binary = GameBinary(archive.read('lib/arm64-v8a/libGame.so'))
    seen = set()
    classes = defaultdict(lambda: defaultdict(list))
    for symbol in binary.symbols.values():
        if symbol['st_info']['type'] != 'STT_FUNC' or symbol['st_size'] < 8 or not symbol['st_value']:
            continue
        if symbol['st_value'] in seen:   # C1/C2 aliases share one body
            continue
        parsed = demangle(symbol.name)
        if not parsed:
            continue
        cls, method = parsed
        top = cls or method
        if LIBRARY.match(top) or LIBRARY.match(method) or top.startswith('_'):
            continue
        seen.add(symbol['st_value'])
        key = f'{cls}::{method}' if cls else f'::{method}'
        classes[subsystem(cls, method)][cls or FREE].append((name_hash(key), symbol['st_size']))
    reference = {'build': 'Android 4.4.243 arm64, a4c441f4943abbcc72e8270ec18248e4358a89e2', 'subsystems': []}
    for title, _ in SUBSYSTEMS + [(OTHER, '')]:
        entries = []
        for cls, routines in sorted(classes[title].items()):
            routines.sort()
            entries.append({'name': cls, 'hashes': ''.join(h for h, _ in routines),
                            'sizes': [s for _, s in routines]})
        reference['subsystems'].append({'name': title, 'classes': entries})
    REFERENCE.parent.mkdir(parents=True, exist_ok=True)
    REFERENCE.write_text(json.dumps(reference, separators=(',', ':')) + '\n', encoding='utf-8')
    print(f'Wrote {REFERENCE.relative_to(ROOT)} ({len(seen)} routines)')


# ---------------------------------------------------------------------------------------------- measuring

def ported_hashes(root):
    pattern = re.compile(r'\b([A-Za-z_]\w*)::(~?[A-Za-z_]\w*)')
    word = re.compile(r'\b([A-Za-z_]\w{3,})\b')
    hashes = set()
    for directory in SOURCE_DIRS:
        base = root / directory
        if not base.exists():
            continue
        for path in base.rglob('*'):
            if path.suffix not in SOURCE_SUFFIXES or 'third_party' in path.parts or 'build' in path.parts:
                continue
            text = path.read_text(encoding='utf-8', errors='replace')
            hashes.update(name_hash(f'{c}::{m}') for c, m in pattern.findall(text))
            hashes.update(name_hash(f'::{w}') for w in set(word.findall(text)))
    return hashes


def measure(root=ROOT, reference=None):
    reference = reference or json.loads(REFERENCE.read_text(encoding='utf-8'))
    done = ported_hashes(root)
    result = {'build': reference['build'], 'bytes': 0, 'portedBytes': 0, 'routines': 0, 'portedRoutines': 0,
              'subsystems': []}
    for sub in reference['subsystems']:
        out = {'name': sub['name'], 'bytes': 0, 'portedBytes': 0, 'routines': 0, 'portedRoutines': 0, 'classes': []}
        for cls in sub['classes']:
            hashes = [cls['hashes'][i:i + 8] for i in range(0, len(cls['hashes']), 8)]
            c = {'name': cls['name'], 'bytes': sum(cls['sizes']), 'routines': len(hashes), 'portedBytes': 0,
                 'portedRoutines': 0}
            for h, size in zip(hashes, cls['sizes']):
                if h in done:
                    c['portedBytes'] += size
                    c['portedRoutines'] += 1
            out['classes'].append(c)
            for k in ('bytes', 'portedBytes', 'routines', 'portedRoutines'):
                out[k] += c[k]
        for k in ('bytes', 'portedBytes', 'routines', 'portedRoutines'):
            result[k] += out[k]
        result['subsystems'].append(out)
    return result


def percent(d):
    return 100 * d['portedBytes'] / d['bytes'] if d['bytes'] else 0.0


# ---------------------------------------------------------------------------------------------- drawing

GREEN, GREY, DARK, INK = '#3fa34d', '#4d5257', '#16191c', '#e6e8ea'
FONT = 'Verdana,DejaVu Sans,sans-serif'


def squarify(values, x, y, w, h):
    """Bruls et al. squarified treemap: rectangles for values (sorted large first) inside x, y, w, h."""
    rects = []
    items = [(v, i) for i, v in enumerate(values) if v > 0]
    total = sum(v for v, _ in items)
    if not items or w <= 0 or h <= 0:
        return [None] * len(values)
    scale = w * h / total
    items = [(v * scale, i) for v, i in items]
    out = [None] * len(values)
    while items:
        short = min(w, h)
        row, rest = [items[0]], items[1:]

        def worst(r):
            s = sum(a for a, _ in r)
            return max(max(short * short * a / (s * s), (s * s) / (short * short * a)) for a, _ in r)
        while rest and worst(row + [rest[0]]) <= worst(row):
            row.append(rest.pop(0))
        s = sum(a for a, _ in row)
        if w >= h:   # a column on the left
            cw = s / h
            yy = y
            for a, i in row:
                out[i] = (x, yy, cw, a / cw)
                yy += a / cw
            x, w = x + cw, w - cw
        else:        # a row along the top
            rh = s / w
            xx = x
            for a, i in row:
                out[i] = (xx, y, a / rh, rh)
                xx += a / rh
            y, h = y + rh, h - rh
        items = rest
    return out


def treemap(result, width=1000, height=560):
    head = 26
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
             f'viewBox="0 0 {width} {height}" font-family="{FONT}">',
             f'<rect width="{width}" height="{height}" fill="{DARK}"/>',
             f'<text x="8" y="18" font-size="13" fill="{INK}">Original game code ported: {percent(result):.2f}% '
             f'({result["portedRoutines"]} / {result["routines"]} routines, by code size)</text>']
    subs = sorted(result['subsystems'], key=lambda s: -s['bytes'])
    boxes = squarify([s['bytes'] for s in subs], 2, head, width - 4, height - head - 2)
    for sub, box in zip(subs, boxes):
        if not box:
            continue
        sx, sy, sw, sh = box
        label = 15 if sh > 40 else 0
        classes = sorted(sub['classes'], key=lambda c: -c['bytes'])
        # tiny classes are merged into one cell so the image stays light
        cutoff = sub['bytes'] * 0.002
        big = [c for c in classes if c['bytes'] >= cutoff]
        small = [c for c in classes if c['bytes'] < cutoff]
        if small:
            big.append({'name': '', 'bytes': sum(c['bytes'] for c in small),
                        'portedBytes': sum(c['portedBytes'] for c in small)})
        cells = squarify([c['bytes'] for c in big], sx + 1, sy + label + 1, sw - 2, sh - label - 2)
        for c, cell in zip(big, cells):
            if not cell:
                continue
            x, y, w, h = cell
            done = c['portedBytes'] / c['bytes'] if c['bytes'] else 0
            parts.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" fill="{GREY}" '
                         f'stroke="{DARK}" stroke-width="0.6"/>')
            if done > 0:
                fh = h * done
                parts.append(f'<rect x="{x:.1f}" y="{y + h - fh:.1f}" width="{w:.1f}" height="{fh:.1f}" '
                             f'fill="{GREEN}" stroke="{DARK}" stroke-width="0.6"/>')
            if c['name'] and w > 5.6 * len(c['name']) + 6 and h > 14:
                parts.append(f'<text x="{x + 3:.1f}" y="{y + 11:.1f}" font-size="9" fill="{INK}">'
                             f'{html.escape(c["name"])}</text>')
        parts.append(f'<rect x="{sx:.1f}" y="{sy:.1f}" width="{sw:.1f}" height="{sh:.1f}" fill="none" '
                     f'stroke="{INK}" stroke-opacity="0.55" stroke-width="1.2"/>')
        title = f'{sub["name"]} {percent(sub):.1f}%'
        if label and sw < 7.4 * len(title) + 8:
            title = f'{percent(sub):.1f}%'
        if label and sw >= 7.4 * len(title) + 8:
            parts.append(f'<text x="{sx + 4:.1f}" y="{sy + 12:.1f}" font-size="11" font-weight="bold" '
                         f'fill="{INK}">{html.escape(title)}</text>')
    parts.append('</svg>')
    return '\n'.join(parts) + '\n'


def badge_colour(p):
    for limit, colour in ((90, '#4c1'), (60, '#97ca00'), (30, '#a4a61d'), (10, '#dfb317'), (3, '#fe7d37')):
        if p >= limit:
            return colour
    return '#e05d44'


def badge(label, value, colour):
    lw, vw = 7 * len(label) + 12, 7 * len(value) + 12
    w = lw + vw
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="20" role="img" '
            f'aria-label="{label}: {value}"><rect width="{lw}" height="20" fill="#555"/>'
            f'<rect x="{lw}" width="{vw}" height="20" fill="{colour}"/>'
            f'<g fill="#fff" font-family="{FONT}" font-size="11" text-anchor="middle">'
            f'<text x="{lw / 2}" y="14">{label}</text><text x="{lw + vw / 2}" y="14">{value}</text></g></svg>\n')


PAGE = '''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>{title}</title><style>
body{{background:#0f1113;color:#e6e8ea;font:15px/1.5 system-ui,sans-serif;margin:0 auto;max-width:1040px;padding:16px}}
a{{color:#7cc4ff}} img{{max-width:100%;height:auto}} table{{border-collapse:collapse;width:100%}}
td,th{{border-bottom:1px solid #2a2f33;padding:4px 8px;text-align:left}} td.n{{text-align:right;font-variant-numeric:tabular-nums}}
.bar{{background:#4d5257;height:8px;width:160px}} .bar i{{background:#3fa34d;display:block;height:8px}}
details{{margin:4px 0}} summary{{cursor:pointer}} small{{color:#9aa3a8}}
</style></head><body>{body}</body></html>
'''


def page(result):
    rows = []
    for sub in sorted(result['subsystems'], key=lambda s: -s['bytes']):
        p = percent(sub)
        gaps = sorted(sub['classes'], key=lambda c: -(c['bytes'] - c['portedBytes']))[:15]
        best = sorted([c for c in sub['classes'] if c['portedRoutines']], key=lambda c: -c['portedBytes'])[:15]
        fmt = lambda c: f'<code>{html.escape(c["name"])}</code> {c["portedRoutines"]}/{c["routines"]}'
        rows.append(
            f'<tr><td><details><summary>{html.escape(sub["name"])}</summary>'
            f'<small>Most ported: {", ".join(map(fmt, best)) or "nothing yet"}<br>'
            f'Biggest gaps: {", ".join(map(fmt, gaps))}</small></details></td>'
            f'<td><div class="bar"><i style="width:{p:.1f}%"></i></div></td><td class="n">{p:.2f}%</td>'
            f'<td class="n">{sub["portedRoutines"]} / {sub["routines"]}</td></tr>')
    body = (f'<h1>CTW-Native progress</h1><p><img src="badge-overall.svg" alt="ported"> '
            f'<img src="badge-routines.svg" alt="routines"></p><img src="progress.svg" alt="progress map">'
            f'<p>Green is the share of each class\'s code that the port names as ported. Reference: {result["build"]}. '
            f'Raw numbers: <a href="progress.json">progress.json</a>. <a href="history.html">Progress history</a>.</p>'
            f'<table><tr><th>Subsystem</th><th></th><th>Code</th><th>Routines</th></tr>{"".join(rows)}</table>'
            f'<p><small>A routine counts once the source names it as <code>Class::Method</code>, the convention for '
            f'ported code; some are partial. Library code the PC build replaces is not counted.</small></p>')
    return PAGE.format(title='CTW-Native progress', body=body)


def write(result, out):
    out.mkdir(parents=True, exist_ok=True)
    p = percent(result)
    (out / 'progress.json').write_text(json.dumps(result, indent=1), encoding='utf-8')
    (out / 'badge-overall.svg').write_text(badge('original code ported', f'{p:.2f}%', badge_colour(p)), encoding='utf-8')
    (out / 'badge-routines.svg').write_text(
        badge('routines', f'{result["portedRoutines"]} / {result["routines"]}', badge_colour(p)), encoding='utf-8')
    (out / 'progress.svg').write_text(treemap(result), encoding='utf-8')
    (out / 'progress.html').write_text(page(result), encoding='utf-8')


def compare(base, head):
    """Markdown for a pull request: the overall and per-subsystem change, and newly ported classes."""
    b, h = (json.loads(Path(p).read_text(encoding='utf-8')) for p in (base, head))
    if h['portedRoutines'] == b['portedRoutines'] and h['portedBytes'] == b['portedBytes']:
        return ''
    lines = [f'**Original code ported:** {percent(b):.2f}% → {percent(h):.2f}% '
             f'({h["portedRoutines"] - b["portedRoutines"]:+d} routines)', '',
             '| Subsystem | Before | After | Routines |', '| --- | --- | --- | --- |']
    old = {s['name']: s for s in b['subsystems']}
    newly = []
    for s in h['subsystems']:
        o = old.get(s['name'])
        if not o or s['portedRoutines'] == o['portedRoutines']:
            continue
        lines.append(f'| {s["name"]} | {percent(o):.2f}% | {percent(s):.2f}% | '
                     f'{s["portedRoutines"] - o["portedRoutines"]:+d} |')
        oc = {c['name']: c for c in o['classes']}
        newly += [f'`{c["name"]}` {oc.get(c["name"], {}).get("portedRoutines", 0)} → {c["portedRoutines"]}'
                  for c in s['classes'] if c['portedRoutines'] != oc.get(c['name'], {}).get('portedRoutines', 0)]
    if newly:
        lines += ['', '<details><summary>Classes</summary>', '', ', '.join(newly), '', '</details>']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description='Measure how much of the original game code the port covers.')
    parser.add_argument('output', type=Path, nargs='?', help='directory for progress.json, badges and pages')
    parser.add_argument('--root', type=Path, default=ROOT, help='source tree to measure')
    parser.add_argument('--index', action='store_true', help='rebuild progress/reference.json from your APK')
    parser.add_argument('--apk', type=Path)
    parser.add_argument('--compare', type=Path, nargs=2, metavar=('BASE', 'HEAD'))
    args = parser.parse_args()
    if args.index:
        build_index(args.apk)
        return
    if args.compare:
        print(compare(*args.compare))
        return
    result = measure(args.root)
    print(f'Original code ported: {percent(result):.2f}% ({result["portedRoutines"]} / {result["routines"]} routines)')
    for s in sorted(result['subsystems'], key=lambda s: -s['bytes']):
        print(f'  {s["name"]:24s} {percent(s):6.2f}%  {s["portedRoutines"]:5d} / {s["routines"]:5d}')
    if args.output:
        write(result, args.output)
        print(f'Wrote {args.output}')


if __name__ == '__main__':
    main()
