# Your APK goes here

Only needed when you build from source. To just play, use the Porter instead (see the main README).

Put your own **GTA: Chinatown Wars Android 4.4.243** APK here, then run `python scripts/setup_game.py` (data for
development) or `python scripts/export_pc.py` (a standalone game folder) from the repository root, or pass
`--apk PATH` to either. APKs and extracted files are ignored by Git.

For Enhancements' optional 3D people, put your own **Liberty City Stories Android 2.4.379** APK at
`input/lcs/GTA-LCS-2.4.379.apk`. Keeping it in this subfolder leaves the CTW setup script's APK selection intact.
The normal development CMake build imports its character assets into `port/build/mods/Enhancements/lcs/`.
Press **V** in the game to use third person and the imported people.
