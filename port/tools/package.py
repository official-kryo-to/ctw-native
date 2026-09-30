"""Builds the release and packs the clean player archive.

    python tools/package.py [--modkit-url URL]

Output (next to the port folder):
    dist/GTA Chinatown Wars/             the folder players get
        GTACTW.exe                       the game (release build: no console window, version info, icon)
        readme.html                      about the port (placeholder for now)
        mods/Mod Menu & SDK.url          shortcut to the mod kit repository (the game ships without mods)
    dist/GTA-Chinatown-Wars-PC-<version>.zip

Rule for the archive: only what a player needs. Never: game files (players bring their own), source code,
the showcase, tools, the mod kit, logs, saves.
"""
import argparse, os, re, shutil, subprocess, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)
ROOT = os.path.dirname(PORT)
DIST = os.path.join(ROOT, "dist")
NAME = "GTA Chinatown Wars"
MODKIT_URL = "https://github.com/kryo-to/ctw-modkit"   # placeholder until the repository exists

README_HTML = """<!doctype html>
<html lang="en">
<head><meta charset="utf-8"><title>GTA: Chinatown Wars - PC port</title></head>
<body>
<p>Hello world</p>
</body>
</html>
"""


def run(cmd):
    print(">", " ".join(cmd))
    subprocess.check_call(cmd, cwd=PORT)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modkit-url", default=MODKIT_URL)
    args = ap.parse_args()

    icon = os.path.join(PORT, "data", "gtactw.ico")
    if not os.path.exists(icon):
        run([sys.executable, os.path.join(HERE, "make_icon.py")])

    build = os.path.join(PORT, "build-release")
    run(["cmake", "-S", ".", "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCTW_RELEASE=ON"])
    run(["cmake", "--build", build, "--target", "ctw_game"])

    version = re.search(r"set\(CTW_VERSION ([0-9.]+)\)", open(os.path.join(PORT, "CMakeLists.txt")).read()).group(1)
    out = os.path.join(DIST, NAME)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(os.path.join(out, "mods"))
    shutil.copy2(os.path.join(build, "GTACTW.exe"), out)
    with open(os.path.join(out, "readme.html"), "w", encoding="utf-8") as f:
        f.write(README_HTML)
    with open(os.path.join(out, "mods", "Mod Menu & SDK.url"), "w", encoding="utf-8") as f:
        f.write("[InternetShortcut]\r\nURL=%s\r\n" % args.modkit_url)

    zpath = os.path.join(DIST, "GTA-Chinatown-Wars-PC-%s.zip" % version)
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for dirpath, _, files in os.walk(out):
            rel = os.path.relpath(dirpath, DIST)
            z.write(dirpath, rel)
            for fn in files:
                z.write(os.path.join(dirpath, fn), os.path.join(rel, fn))
    print("\n%s\n%s" % (out, zpath))
    for dirpath, _, files in os.walk(out):
        for fn in files:
            p = os.path.join(dirpath, fn)
            print("  %-40s %9d bytes" % (os.path.relpath(p, out), os.path.getsize(p)))


if __name__ == "__main__":
    main()
