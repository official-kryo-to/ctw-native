#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kryo.to
# See LICENSE in the repository root.
#
# Makes the game's data/ folder from your own GTA: Chinatown Wars Android APK (4.4.243), as the Porter does on
# Windows: ./port-apk.sh path/to/your.apk
# Needs Python 3.10+ with venv; the setup's three packages go into .setup-venv next to this script.
set -e
here=$(cd "$(dirname "$0")" && pwd)
if [ $# -ne 1 ] || [ ! -f "$1" ]; then
    echo "usage: $0 path/to/your.apk" >&2
    exit 2
fi
apk=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
venv="$here/.setup-venv"
if [ ! -f "$venv/.ready" ]; then
    rm -rf "$venv"
    if ! python3 -m venv "$venv" || ! "$venv/bin/python" -m pip install --quiet -r "$here/setup/requirements-setup.txt"; then
        rm -rf "$venv"
        echo "Could not set up Python for the setup. It needs Python 3.10+ with venv and pip" >&2
        echo "(on Debian or Ubuntu: sudo apt install python3-venv), and the internet for its three packages." >&2
        exit 1
    fi
    touch "$venv/.ready"
fi
"$venv/bin/python" "$here/setup/setup_game.py" --apk "$apk" --data "$here/data"
echo "Done. Start the game with $here/GTACTW"
