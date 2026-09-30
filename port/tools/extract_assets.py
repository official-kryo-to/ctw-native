"""Extract assets/ from a user-supplied GTA:CTW APK into port/data. No game data is bundled with this project."""
import sys, zipfile, os
apk, out = sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else 'data'
z = zipfile.ZipFile(apk)
for i in z.infolist():
    if i.filename.startswith('assets/') and not i.is_dir():
        dst = os.path.join(out, i.filename[len('assets/'):])
        os.makedirs(os.path.dirname(dst) or '.', exist_ok=True)
        with z.open(i) as s, open(dst, 'wb') as d:
            while (b := s.read(1 << 20)): d.write(b)
print('done')
