# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kryo.to
# See LICENSE in the repository root.
"""Progress over time: the last first-parent commit of each day, measured against today's reference index so the
series stays comparable. Writes history.json, history.svg and history.html into OUT_DIR.

    python scripts/progress_history.py OUT_DIR [--ref main]
"""
import argparse
import html
import io
import json
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import progress  # noqa: E402

ROOT = progress.ROOT


def git(*args, binary=False):
    out = subprocess.run(['git', *args], cwd=ROOT, capture_output=True, check=True).stdout
    return out if binary else out.decode()


def daily_commits(ref):
    days = {}
    for line in git('log', '--first-parent', '--format=%H %cs', ref).splitlines():
        sha, day = line.split()
        days.setdefault(day, sha)   # newest first: the day's last commit
    return sorted((day, sha) for day, sha in days.items())


def measure(sha, reference):
    paths = [p for p in progress.SOURCE_DIRS if git('ls-tree', '--name-only', sha, p).strip()]
    if not paths:
        return None
    with tempfile.TemporaryDirectory() as tmp:
        data = git('archive', sha, *paths, binary=True)
        with tarfile.open(fileobj=io.BytesIO(data)) as archive:
            archive.extractall(tmp, filter='data')
        return progress.measure(Path(tmp), reference)


def chart(points, width=900, height=300):
    left, right, top, bottom = 48, 16, 16, 34
    top_value = max(5, -(-max(p['percent'] for p in points) // 5) * 5)
    x = lambda i: left + (width - left - right) * (i / max(1, len(points) - 1))
    y = lambda v: top + (height - top - bottom) * (1 - v / top_value)
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
             f'viewBox="0 0 {width} {height}" font-family="{progress.FONT}" font-size="11">',
             f'<rect width="{width}" height="{height}" fill="{progress.DARK}"/>']
    for k in range(6):
        v = top_value * k / 5
        parts.append(f'<line x1="{left}" x2="{width - right}" y1="{y(v):.1f}" y2="{y(v):.1f}" stroke="#2a2f33"/>'
                     f'<text x="{left - 6}" y="{y(v) + 4:.1f}" text-anchor="end" fill="#9aa3a8">{v:g}%</text>')
    line = ' '.join(f'{x(i):.1f},{y(p["percent"]):.1f}' for i, p in enumerate(points))
    parts.append(f'<polyline points="{line}" fill="none" stroke="{progress.GREEN}" stroke-width="2.5"/>')
    for i, p in enumerate(points):
        parts.append(f'<circle cx="{x(i):.1f}" cy="{y(p["percent"]):.1f}" r="3.5" fill="{progress.GREEN}">'
                     f'<title>{p["day"]}: {p["percent"]:.2f}% ({p["portedRoutines"]} routines)</title></circle>')
    parts.append(f'<text x="{left}" y="{height - 10}" fill="#9aa3a8">{points[0]["day"]}</text>'
                 f'<text x="{width - right}" y="{height - 10}" text-anchor="end" fill="#9aa3a8">'
                 f'{points[-1]["day"]} · {points[-1]["percent"]:.2f}%</text></svg>')
    return '\n'.join(parts) + '\n'


def page(points):
    rows = ''.join(f'<tr><td>{p["day"]}</td><td><code>{p["commit"][:7]}</code></td>'
                   f'<td class="n">{p["percent"]:.2f}%</td><td class="n">{p["portedRoutines"]}</td></tr>'
                   for p in reversed(points))
    body = ('<h1>Progress history</h1><img src="history.svg" alt="progress over time">'
            '<p>The last commit of each day, measured against the current reference index, so the series is '
            'comparable across days. Hover a point for the numbers. Raw numbers: <a href="history.json">history.json</a>. '
            '<a href="progress.html">Back to the current progress</a>.</p>'
            f'<table><tr><th>Day</th><th>Commit</th><th>Code</th><th>Routines</th></tr>{rows}</table>')
    return progress.PAGE.format(title='CTW-Native progress history', body=body)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('output', type=Path)
    parser.add_argument('--ref', default='HEAD')
    args = parser.parse_args()
    reference = json.loads(progress.REFERENCE.read_text(encoding='utf-8'))
    points = []
    for day, sha in daily_commits(args.ref):
        result = measure(sha, reference)
        if result:
            points.append({'day': day, 'commit': sha, 'percent': round(progress.percent(result), 3),
                           'portedRoutines': result['portedRoutines'], 'routines': result['routines']})
    if not points:
        sys.exit('No measurable commits.')
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'history.json').write_text(json.dumps(points, indent=1), encoding='utf-8')
    (args.output / 'history.svg').write_text(chart(points), encoding='utf-8')
    (args.output / 'history.html').write_text(page(points), encoding='utf-8')
    print(f'{len(points)} days, latest {points[-1]["percent"]:.2f}%')


if __name__ == '__main__':
    main()
