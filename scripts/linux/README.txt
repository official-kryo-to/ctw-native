CTW-Native for Linux (x86-64)

This folder is the game without its data: it contains no game files.

1. Make the data from your own GTA: Chinatown Wars Android APK (version 4.4.243):

       ./port-apk.sh path/to/your.apk

   This needs Python 3.10 or newer with venv (on Debian or Ubuntu: sudo apt install python3-venv). It writes data/
   next to the game.

2. Start the game:

       ./GTACTW

Saves go into saves/ next to the game. Mods go into mods/; the mod menu (F4) and the example mods are already there.
The keys are listed in CONTROLS.md on the project page. The game needs OpenGL; it was built on Ubuntu 22.04 and runs
on that or anything newer.

Source code, license (GPL-3.0-only) and third-party notices: https://github.com/official-kryo-to/ctw-native
