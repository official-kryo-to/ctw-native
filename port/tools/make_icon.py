"""Makes data/gtactw.ico (the exe icon) from the launcher icon in your own GTA: Chinatown Wars APK.
   python tools/make_icon.py [path/to/game.apk]      (default: the first .apk next to the port folder)
The icon is the game's, so like all game files it stays in data/ and is never part of the source."""
import glob, io, os, sys, zipfile
from PIL import Image

here = os.path.dirname(os.path.abspath(__file__))
port = os.path.dirname(here)
apk = sys.argv[1] if len(sys.argv) > 1 else (glob.glob(os.path.join(os.path.dirname(port), "*.apk")) or [None])[0]
if not apk:
    sys.exit("no APK given or found next to the port folder")
z = zipfile.ZipFile(apk)
for name in ("res/mipmap-xxxhdpi-v4/ic_launcher.png", "res/mipmap-xxhdpi-v4/ic_launcher.png"):
    if name in z.namelist():
        img = Image.open(io.BytesIO(z.read(name))).convert("RGBA")
        break
else:
    sys.exit("no launcher icon in " + apk)
out = os.path.join(port, "data", "gtactw.ico")
img.resize((256, 256), Image.LANCZOS).save(out, sizes=[(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (16, 16)])
print("wrote", out)
