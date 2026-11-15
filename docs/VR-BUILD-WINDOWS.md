# Building and changing Sly 2 VR on Windows

For a Claude session (or a person) on the player's Windows PC who wants to change the VR mod's code. To tune
settings without code, see `Sly2-VR-0.1.2-TUNING.md` instead. Read [VR-PROTOTYPE.md](VR-PROTOTYPE.md) for how the mod
works.

## What this source is

- **SafeHouse v0.1.71:** commit `c70803c` of <https://github.com/FridiNaTor1/SafeHouse>, a native C++20 port of Sly 2.
- **Plus the Sly 2 VR mod 0.1.2**, as uncommitted changes on top.
- `sly2-vr-changes.patch` at the root of the bundle is every change the mod makes against `c70803c`, so you can see what
  is the mod and what is SafeHouse.
- No game data is included or ever needed in the source. The game data comes from the player's own disc through
  SafeHouse.

## Rules

- **Keep the normal game unchanged.**
  - All VR code is behind `#if SLY2N_VR` (built with `-DSLY2_VR=ON`) and `vr::Active()` / `vr::FFirstPerson()` at run
    time.
  - `vr.h` has inline no-op stubs for the flat build.
  - Build the flat game too (below) after a change outside the VR files.
- **Keep the code style.** Terse comments that say what and why, often in parentheses, in British spelling. State in
  `vr.cpp` lives in `s.xxx`, and boolean functions start with F.
- **Never change the player's saves** (`%LOCALAPPDATA%\Programs\Sly2VR\user\`) or the SafeHouse install. Never put
  game data in a package.
- **Ask the player before downloading anything.** Give the file name, the source and the size.
- **Ship changes as an update zip** through the packager and `Update.bat` (below). It backs up what it replaces, and
  `Rollback.bat` undoes it. Don't copy files into `bin\` by hand.

## One-time setup

1. **Put the source in a path without spaces**, for example `C:\sly2vr\src`. The build passes a forced-include path
   that breaks on spaces.
2. **Tools** (ask before downloading each):
   - **LLVM-MinGW:** <https://github.com/mstorsjo/llvm-mingw/releases>, the `ucrt` x86_64 Windows build, e.g.
     `llvm-mingw-20261104-ucrt-x86_64.zip`. Unzip it to `C:\llvm-mingw`. The Mac session used the 20261104 release.
     **MSVC does not build Sly 2.**
   - **CMake** 3.25 or newer (the Mac used 4.4.5), **Ninja** 1.11 or newer, **Python 3.10+**, and optionally **Git**.
3. **Track your own changes:** in `C:\sly2vr\src`, run `git init`, `git add -A` and
   `git commit -m "Sly 2 VR 0.1.2 as received"`. `git diff` then shows exactly what you changed, to send back to the
   Mac session.

## Build

In PowerShell:

```powershell
cmake -S C:\sly2vr\src -B C:\sly2vr\build -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=C:/sly2vr/src/cmake/mingw-toolchain.cmake -DCANE_MINGW_ROOT=C:/llvm-mingw `
  -DCMAKE_BUILD_TYPE=Release -DCANE_BUILD_GAME=OFF -DCANE_BUILD_LAUNCHER=OFF -DCANE_BUILD_SLY3=OFF `
  -DCANE_BUILD_SLY2=ON -DSLY2_VR=ON
cmake --build C:\sly2vr\build --target Sly2-Release
```

- The first configure downloads four libraries from GitHub, each checked by its SHA-256:
  - OpenXR SDK `release-1.1.65`
  - GLFW 3.4
  - FAudio 25.02
  - SDL2
- The result is `C:\sly2vr\build\bin\Sly2-Release.exe`. Later builds only recompile what changed.
- **The flat game** (no VR), to check that a change didn't break it: use the same command with `-B C:\sly2vr\build-flat`
  and `-DSLY2_VR=OFF`.
- This exact command was run on the Mac with the same toolchain file (cross-compiling). The toolchain file supports a
  Windows host too, but the first Windows configure is new: if it fails, read the error. It is usually a path or a
  missing tool.

## Package and install a change

1. Build (above).
2. Make an update zip against the full 0.1.0 package, which the player has in Downloads:

   ```powershell
   cd C:\sly2vr\src
   python tools\package_vr.py --build C:\sly2vr\build --mingw C:\llvm-mingw --version 0.1.2.1 `
     --out C:\sly2vr\out --openxr-license C:\sly2vr\build\_deps\openxr-src\LICENSE `
     --update-from "$env:USERPROFILE\Downloads\Sly2-VR-0.1.0.zip" --notes "What changed, in a line or two"
   ```

   - Use your own version numbers (`0.1.2.1`, `0.1.2.2` and so on), so they don't clash with the Mac's.
   - The packager checks every DLL, writes `manifest.json` with checksums, and puts only the changed files in
     `Sly2-VR-update-<version>.zip`.
3. Close the game. Drag the zip onto `Update.bat` in `%LOCALAPPDATA%\Programs\Sly2VR`. If it's worse, `Rollback.bat`
   restores the previous files.

## Where things are (the mod's code)

All of it is in `Sly2-Release/`.

| File | What |
| --- | --- |
| `vr.h` / `vr.cpp` | OpenXR: the session, eyes, swapchains and layers (projection, HUD panel, wristwatch quad). The controllers (`ReadControllers`: buttons, tap/hold, sprint toggle, chords, swings, snap turn, grips and grab zones, the holster, attack rules). The T-pose calibration, haptics (`Pulse`), the watch gesture and laser, glider lines, Sly size. The tunable constants are `constexpr` near their functions. |
| `vrinteract.h` / `.cpp` | At step time (the world changes only here): pockets (reach, then grip to steal), climbing (grab zones, Circle, hand over hand, holding ledges), the watch's gadget list and pick. |
| `vrbody.h` / `.cpp` | Drawing: the hands (found from the bind pose), the cane by its shape (`MeasureCane`) in the grip or holstered, the body hidden. The debug skeleton dump (`DumpVrSkeleton`). |
| `vrprofile.h` / `.cpp` | `Sly2VR.ini`: defaults, keys, the ini template, layout migration, `FSave`. |
| `vrui.h` / `.cpp` | The T-pose message plate, the watch face (`DrawVrWatch`, `IWatchEntryAt`). |
| `vraim.h` / `.cpp` | Aiming throws and darts with the controller. |
| `menu.cpp` | Options > VR Settings (`AentVrSettings`, `StepVr`, the left/right handling in state 7). |
| `game.cpp` | Wiring: the VR draw block (eyes, hands, HUD layer, watch), the cinematic camera rules, the step-time `VrStepInteract` call. |
| `pocket.cpp` / `po.cpp` / `rumble.cpp` / `gadget.cpp` | Hooks: no prize card in VR, glider line steering, rumble to the controllers, the gadget slot accessor. |
| `../packaging/vr/` / `../tools/package_vr.py` | The player's package: launchers, scripts, README-FIRST.txt, CONTROLS.txt, and the packager. |

## Debug tools

- **Verbose VR log:** the "First run (check the hands)" launcher, or `SLY2N_VR_BONES=1`. It logs the bone table, the hands,
  the T-pose and the `vr cane:` measurement.
- **Headless skeleton dump** (no headset, runs the game flat for a few seconds):
  - Run from `C:\sly2vr\build\bin`:

    ```powershell
    $env:SLY2N_SKELDUMP = "C:\sly2vr\skel.txt"; $env:SLY2_NOMOVIE = "1"
    .\Sly2-Release.exe --data-dir "<the dataDir in %LOCALAPPDATA%\Programs\Sly2VR\config.json>" `
      --user-dir C:\sly2vr\scratch-user --world 2 --headless --frames 200
    ```

  - Use an empty scratch `--user-dir`, never the player's.
  - World 2 is Paris, where Sly, Bentley and Murray are all loaded; world 0 is the Cairo museum.
  - The file lists each character's bones, the hands chosen, and the cane's measurement (expected on this disc: "the
    shaft fitted on 55, 15.2 from the hand's record; the hook (the cane point) 85.8 out").
- **Settings tests:** `tools/vrtests/` holds the `Sly2VR.ini` parser and migration tests. Build one with LLVM-MinGW's
  clang:

  ```powershell
  C:\llvm-mingw\bin\x86_64-w64-mingw32-clang++ -std=c++20 -I Sly2-Release -I Libraries/include -I . `
    tools/vrtests/initest.cpp Sly2-Release/vrprofile.cpp -o initest.exe
  ```

  Then run `.\initest.exe test.ini`. For the migration test, copy `tools\vrtests\v011.ini` to `run.ini` and run
  `.\migtest.exe run.ini 011`; do the same for `010`. Each prints `all passed`.

## Sending a change back to the Mac session

Run `git diff > my-change.patch` in `C:\sly2vr\src` and send the patch along with what it fixes and how it was tested
(which build, headset or not). The Mac session merges it into the main copy, so the next official update includes it.
