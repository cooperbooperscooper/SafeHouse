# Sly2VR.ini tests

The VR settings parser and its layout migration (`Sly2-Release/vrprofile.cpp`). See `docs/VR-BUILD-WINDOWS.md` for the
build commands. `initest <file>` writes a fresh Sly2VR.ini at `<file>` and checks the defaults, saves, overrides and the
T-pose keys. `migtest <copy of v010.ini, v011.ini or v012.ini> 010|011|012` checks that a 0.1.0, 0.1.1 or 0.1.2 file moves on to layout 4 and
keeps the player's own values. Run `migtest` on a copy: it rewrites the file. Each prints `all passed`.
