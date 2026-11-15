# Sly 2 VR mod: handoff for the Windows PC

You are helping test and fix **Sly 2 VR 0.1.2**, an OpenXR VR mod for **SafeHouse**, a native C++20 PC port of the
PS2 game *Sly 2: Band of Thieves* (upstream: https://github.com/FridiNaTor1/SafeHouse). The mod was written and built
in another Claude session on a Mac (cross-compiled for Windows with LLVM/MinGW). 0.1.0 was played once on this PC
(third person nearly right; first person playable but the hands were turned wrong, the body popped in and out, the
letterbox floated). 0.1.1 fixes those: a T-pose calibration at the title, cane only by default, cutscenes through the
game's camera with no bars, sprint on the left stick click, pause on a long Y press. 0.1.2 (from the player's
0.1.1 test notes, `Sly2-VR-0.1.2-feedback.md`) makes first person physical, Contractors-style: Sly's hands on the
controllers, the cane held in the grip by its own shape and holstered on the back (grip behind the right shoulder),
pickpocketing (0.1.3: hold B at a pocket, then hook and flick the cane; no blue prize card), grab climbing, a wristwatch gadget picker on the right
wrist, paraglider line steering, haptics, and Options > VR Settings. The player cares most about **first person**.

The player uses a **Meta Quest 3 streamed with Steam Link** (SteamVR is the OpenXR runtime).

Ground rules:
- Never edit or delete the player's saves (`%LOCALAPPDATA%\Programs\Sly2VR\user\`, `%LOCALAPPDATA%\ProjectCane\`).
- Never modify the SafeHouse install or its game data, and never hand-edit files in `bin\`.
- Changes you can make freely: `Sly2VR.ini`, and (carefully) the PowerShell scripts in `scripts\`.
- No game data is redistributable: Sly 2 comes from the player's own disc image through SafeHouse.
- Code changes: on a `pc/<what>` branch and a pull request in the private repo (docs/VR-BUILD-WINDOWS.md), reviewed and merged by the Mac session; without repo access, back to the Mac session (see "Escalating").

---

Latest update: `Sly2-VR-update-0.1.2.zip` (4.7 MB, SHA-256 `9e15c5673757ced642fde0508e4f47199618405c554d801c2faf5b54c98e6679`,
built against the full 0.1.0 package, so it applies over 0.1.0 or 0.1.1). Previous: `Sly2-VR-update-0.1.1.zip` (4.7 MB, SHA-256 `2509391a35b11ee909d53a35765b18c40df26837343da2ddf05cb2b36049c6bd`),
applied on top of `Sly2-VR-0.1.0.zip` with Update.bat.

**Source code:** the player's private GitHub repo <https://github.com/cooperbooperscooper/sly2-vr> (`main` = the current
release); this file is in it as `docs/handoff/`. To change code, follow `docs/VR-BUILD-WINDOWS.md` there: a fix goes on a
`pc/<what>` branch and a **pull request**, which is where you and the Mac session talk (sign your comments **[PC]**).
Tuning without code: `Sly2-VR-0.1.2-TUNING.md`.

## 1. What's on this PC

| Thing | Where |
| --- | --- |
| SafeHouse launcher (v0.1.71, setup install) | `%LOCALAPPDATA%\Programs\SafeHouse` |
| Sly 2 game data (installed by SafeHouse from the disc) | `%LOCALAPPDATA%\ProjectCane\installations\sly2` (extra regions: `sly2-<region>`) |
| Normal Sly 2 saves and settings (do not touch) | `%LOCALAPPDATA%\ProjectCane\user\sly2` |
| **The VR mod** | `%LOCALAPPDATA%\Programs\Sly2VR` |

Inside `%LOCALAPPDATA%\Programs\Sly2VR`:

| Path | What it is |
| --- | --- |
| `bin\Sly2-Release.exe` + DLLs | The VR-enabled build of SafeHouse's Sly 2 (plus SDL2, FFmpeg, libc++, libunwind, libwinpthread). |
| `user\` | The VR profile: a copy of the player's Sly 2 saves and settings made at install (`Mods` is a junction to the real one). |
| `Sly2VR.ini` | The VR settings (written by the game the first time VR starts; updates never replace it). |
| `config.json` | Written by Install: `dataDir` (the Sly 2 install used) and `userSource`. |
| `logs\sly2vr-<date>-<time>-<mode>.log` | One log per run (last 20 kept); `logs\update.log` for updates. |
| `backup\` | Files replaced by updates (for Rollback). |
| `updates\` | Drop update zips here (or drag them onto `Update.bat`). |
| `version.txt`, `manifest.json` | The installed version; the package's file list with SHA-256s. |
| `scripts\*.ps1` | install, launch, update, rollback, collect-logs, common (PowerShell 5.1). |
| `*.bat` | Double-click wrappers for the scripts (also in the Start menu folder "Sly 2 VR"). |

## 2. How a VR run starts

`Play VR - <mode>.bat` → `scripts\launch.ps1 -Mode fp|stereo|screen [-Bones]`, which:
1. reads `config.json`, sets `SLY2N_VR=<mode>`, `SLY2N_VR_INI=<install>\Sly2VR.ini` (and `SLY2N_VR_BONES=1` for the
   "First run (check hands)" launcher),
2. runs `bin\Sly2-Release.exe --data-dir "<dataDir>" --user-dir "<install>\user"` with `bin\` as the working folder,
3. writes a header (version, Windows, GPU, **OpenXR runtime**, paths), then the game's stdout and stderr, then the exit
   code, into the log.

Modes: `screen` = the normal game on a big floating screen (lowest risk). `stereo` = the world in 3D around you from
the game's camera, the HUD on a panel. `fp` = first person: eyes at the character's head, only its hands drawn on the
controllers (Sly's cane in his right hand), left stick walks where you look, right stick snap-turns, swings attack.

Without `SLY2N_VR` the same exe is the normal flat game. If OpenXR fails, the game prints why and carries on flat.

## 3. Quest 3 + Steam Link setup

1. PC: Steam + **SteamVR** installed. Quest: **Steam Link** app (Meta Horizon Store). PC wired to the router, Quest on
   5/6 GHz Wi-Fi.
2. Open Steam on the PC, open Steam Link on the Quest, pair (code on the PC). SteamVR starts.
3. **SteamVR must be the active OpenXR runtime:** SteamVR Settings → **OpenXR** → *Set SteamVR as OpenXR Runtime*.
   (The Meta Quest Link app often takes it over.) Check: the log header line `OpenXR runtime:` must say SteamVR. The
   registry value is `HKLM\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime`.
4. Start a "Sly 2 VR" launcher from the Start menu (before putting the headset on, or via SteamVR's Desktop view).
5. The Quest controllers reach the game as `/interaction_profiles/oculus/touch_controller`; the mod suggests its own
   bindings for that profile. If SteamVR's binding UI offers changes, keep the defaults.

## 4. Test order (from README-FIRST.txt)

Big screen → Stereo → "First run (check the hands)" (first person + bone printout) → each character (Sly, Bentley,
Murray), the binocucom, a throw, a turret, Bentley's darts. After each run: "Collect logs" → zip on the desktop.

## 5. Reading a log

Lines the mod prints (stdout and stderr; stdout may be cut short if the game crashes):

| Line | Meaning |
| --- | --- |
| `vr: settings from <path>` / `vr: wrote the default settings to <path>` | Sly2VR.ini loaded / created. |
| `vr: started (<mode> mode, scale <n> units/m, ..., motion controllers)` | **OpenXR is up.** "no motion controllers" = the action set failed (controls fall back to gamepad / keyboard). |
| `vr: xrCreateInstance (is a VR runtime installed and running?) failed: ...` | No OpenXR runtime: SteamVR not set as runtime, or not running. |
| `vr: xrGetSystem (is the headset connected?) failed: ...` | Runtime found, headset not connected (Steam Link not connected yet). |
| `vr: <call> failed: <XR_ERROR_...> (<code>)` | Any other OpenXR failure; the call name says where. |
| `vr: the runtime needs OpenGL x.y, the context is a.b` | GPU driver too old for the runtime's OpenGL needs. |
| `vr: controllers: /interaction_profiles/...` | Which controller profile the runtime picked (Touch expected). |
| `vr hands: ...` (with `-Bones`) | The player's bone list, an index each, and which became the hands ("found by ..."). |
| `vr: hand calibration ...` / `vr: height calibration ...` / `vr: saved [...] ...` | Helper results written into Sly2VR.ini. |
| `vr: paused (the headset lost focus ...)` | Auto-pause fired (headset off / system menu). |
| `vr: the session ended; the game goes on in the window` | Runtime closed the session (Steam Link disconnected, SteamVR quit). |
| `vr: ... = ...: not understood (kept as before)` | A typo in Sly2VR.ini. |

## 6. Light fixes you can do (no code)

### Sly2VR.ini
Open with "Sly 2 VR - Settings" (Notepad). `[default]` applies to all; `[sly]`, `[bentley]`, `[murray]` override per
character. **Reload in game:** hold the left stick click + A (or Ctrl+F8 on the keyboard). Keys:

| Key | Default | Notes |
| --- | --- | --- |
| `player_eye_height` | 0 | Set by the T-pose (or the height helper), metres. 0 = use `scale`. |
| `tpose_left` / `tpose_right` / `tpose_span` | (none) | Written by the T-pose. Delete the two `tpose_` lines (or use Options > Recalibrate VR) to be asked again. |
| `show_hands` | 1 | First person: 1 = Sly's hands on the controllers, 0 = only the cane. |
| `sly_size` / `cane_size` | 1.1 / 0.9 | You as a bigger Sly (the world looks smaller); the drawn cane's size. Also on Options > VR Settings. |
| `cane_rot` / `cane_offset` | 0,0,0 | The cane's turn (degrees) and shift (cm) in the grip (grip axes: x palm, y back of the hand, z little finger). |
| `gadget_menu` | watch | `watch` = the right-wrist picker; `button` = Y opens the game's gadget menu. |
| `pickpocket` | hold | `hold` = hold B at a pocket (no attacks), hook and flick; `grab` = grip at it, no B; `button` = the game's B. |
| `climb` | grab | `grab` = reach and grip (B still works); `button` = B only. |
| `glide` | lines | `lines` = both hands raised and gripping steer the paraglider; `stick` = the stick. |
| `scale` | 100 | Game units per metre of the player's movement (a guess). World too small → lower; too big → raise. |
| `mirror` | left | Desktop window: `left` (left eye + HUD), `eye`, `hud`. |
| `eye` | Sly 25, Bentley 10, Murray 45 | First person: eye height above the character's centre, game units (guesses). |
| `hand_rot` / `hand_rot_left` | auto | `auto` = from the T-pose. A value in a character's section (the X helper writes one) wins for that character; the T-pose resets them to auto. |
| `forearm` | 1 | Show a bit of forearm with each hand. |
| `hands` | auto | `left,right` bone indices from the `-Bones` printout when the automatic choice is wrong. |
| `swing_speed` | 2.5 | m/s for a swing gesture. Too many accidental attacks → raise it. |
| `swing` / `swing_up` | square / triangle | What a swing / upward swing presses (`none` to disable). Bentley: swing_up = none. |
| `a b x y trigger_left trigger_right grip_left grip_right stick_click_left stick_click_right menu trackpad_right` | see file | Each physical control → a PS2 button: `cross circle square triangle l1 r1 l2 r2 l3 r3 start select up down left right sprint_toggle none`. `<control>_hold` = its long press (0.5 s): `y_hold = start` (pause), `stick_click_left_hold = l3`, `stick_click_right_hold = r2`. `menu = none` (Steam Link owns it). Bentley: trigger_right = r1 (darts), grip_right = square (swing). |

Environment overrides for one-off tests (set before running `Sly2-Release.exe` by hand): `SLY2N_VR_SCALE`,
`SLY2N_VR_EYE`, `SLY2N_VR_SWING`, `SLY2N_VR_HAND=x,y,z`, `SLY2N_VR_HANDS=l,r`, `SLY2N_VR_FOREARM`,
`SLY2N_VR_PLAYER_EYE`, `SLY2N_VR_MIRROR`, `SLY2N_VR_LEVEL=0` (keep the game camera's pitch/roll in stereo),
`SLY2N_VR_SCREEN=<m>` (panel distance), `SLY2N_VR_AUTOPAUSE=0`.

### In-game helpers (hold the LEFT stick click, then press)
A reload Sly2VR.ini · B recentre · X fine-tune one character's hands (press, match your hands to the frozen ones,
press again) · Y height only (stand or sit as you play, press). Keyboard: F8 recentre, Ctrl+F8 reload, Shift+F8 hand
cal, Alt+F8 height cal. Tapping the left stick click alone toggles sprint; holding it is L3 (world stats).

### 0.1.2 symptoms
| Symptom | Try |
| --- | --- |
| The cane sits wrong in the hand | Options > VR Settings > Cane Turn / Cane Shift (live). Log with `-Bones`: the `vr cane:` line (shaft found, grip, hook). |
| A hand is on the wrong bone | `-Bones` log `vr hands:` table; `hands = L,R` in that character's section. |
| The watch never opens / is sideways | Inside of the right wrist toward the eyes, hand within 0.65 m of the head; `SLY2N_VR_WATCH_FLIP=1` turns it 180 degrees; `gadget_menu = button` falls back to Y. |
| Grabs don't happen | The zone ticks the hand when in reach (pockets: hold B first, with `pickpocket = hold`); else `climb = button` / `pickpocket = grab` or `button`. |
| Stray sprint / glider when grabbing | Note where; the grip's R1 is held back 2 steps for a grab to take it. |

### The T-pose (0.1.1)
At the title after the Sucker Punch logo (first person only, once until saved): stand straight, face forward, arms
straight out, hold both triggers still for 2 s (a bar fills). Hints on the plate say what's off (arms not level, not
facing forward, moved). B = later. Options > "Recalibrate VR (T-pose)" or "Sly 2 VR - Calibrate" asks again. The log
says `vr: T-pose saved (arms X m apart, eyes Y m above the floor)`. With `-Bones`, `vr hands: T-pose:` lines say what
it derived from each character's skeleton.

### Symptom → first thing to try
| Symptom | Try |
| --- | --- |
| Nothing in the headset; log shows `xrCreateInstance ... failed` | Set SteamVR as the OpenXR runtime; start Steam Link first. |
| `xrGetSystem ... failed` | Connect Steam Link before launching. |
| Game shows on the PC only, no `vr:` lines at all | Launched outside the .bat (no `SLY2N_VR`), or an old exe. |
| World feels tiny / huge | Height calibration (left stick click + Y), or change `scale`. |
| Eyes too high / low (first person) | Change `eye` under the character's section, reload. |
| Hands wrong bones (fingers float, wrong arm) | Run "First run (check hands)", read the `vr hands:` table, set `hands = L,R` for that character. |
| Cane / hands twisted on the controllers | Redo the T-pose (Options > Recalibrate VR); still off for one character → X helper for that character. Log line `T-pose: ... (hand_rot is used)` means the skeleton check failed: send logs. |
| T-pose never finishes | Read the hint on the plate; the arms must be level at shoulder height, at least 0.9 m apart, head facing between them, still for 2 s. |
| Cutscene shows first person, or gameplay shows the cutscene camera | Note where (level, moment) and send logs (code issue). |
| Accidental attacks while reaching | Raise `swing_speed`, or set `swing = none` and use the trigger. |
| HUD is a black box / missing / washed out | Note which mode; try `mirror = eye` vs `left` to compare the desktop; send logs (code issue). |
| Stutters | Lower Steam Link / SteamVR resolution; PC wired; close other apps. |
| Install can't find Sly 2 | SafeHouse not installed with the setup, or Sly 2 not installed in it yet; a portable SafeHouse needs its folder picked. |
| "Windows protected your PC" | More info → Run anyway (unsigned builds). |

### Updates
Drag `Sly2-VR-update-<version>.zip` onto `Update.bat` (checks SHA-256s, backs up, never touches `user\`,
`Sly2VR.ini`, `logs\`). `Rollback.bat` undoes the last update. Close the game first. Keep `Sly2-VR-0.1.0.zip` (the full package; updates
build on it). After 0.1.1, run `Install.bat` from `%LOCALAPPDATA%\Programs\Sly2VR` once to add the Calibrate shortcut
(Update.bat already refreshes shortcuts; this is just in case).

## 7. Escalating to the Mac session

Send back: the "Collect logs" zip (desktop), the mode, the character and place in the game, and exactly what was seen
(a photo of the headset view helps). The Mac session makes update zips. Known code-level gaps in 0.1.0:
- Cutscenes, hacking, the RC missions and the vault dial still show in 3D instead of on the floating screen.
- The binocucom's zoom isn't visible in the headset.
- Murray's hands are a guess; Bentley's come from the game's swing-hand record (unverified).
- HUD transparency was fixed after review but is unverified on hardware.

## 8. Code map (only useful if the source is on this PC)

The source is the SafeHouse repo on branch `feat/sly2-openxr-prototype`, based on upstream commit `c70803c`
(SafeHouse v0.1.71). Design notes: `docs/VR-PROTOTYPE.md`. Main files (all in `Sly2-Release/` unless noted):

| File | Role |
| --- | --- |
| `vr.h` / `vr.cpp` | OpenXR session, swapchains, frame loop, actions and bindings, helpers, auto-pause, aim ray, laser pointer, HUD panel, desktop mirror. Whole file under `#if SLY2N_VR`; `vr.h` has no-op stubs otherwise. |
| `vrprofile.h` / `.cpp` | Sly2VR.ini read / write, per-character profiles, the default file text. |
| `vrbody.h` / `.cpp` | First-person hands: bone moves onto the grips, other bones collapsed, hand finding. |
| `vraim.h` / `.cpp` | The controller aim helpers used by `gang.cpp` (darts), `pccharacter.cpp`, `jtthrow.cpp`, `murraycarry.cpp`, `turret.cpp`. |
| `render.cpp/.h` | `VIEWOVERRIDE`: draw a scene pass from an eye's matrix. |
| `game.cpp` | Per-frame VR: character → profile, first-person camera, per-eye draws, HUD layer, panel, mirror, eye interpolation. |
| `joy.cpp`, `po.cpp`, `cplcy.cpp`, `cm.cpp` (`CmView`), `hud.cpp` (`g_fHudWritesAlpha`), `moviegl.cpp`, `display.cpp`, `promptglyph.cpp` | Input merge, head-relative walking, binocucom by head, head-view visibility, HUD alpha, movie frames, VSync, Quest button prompts. |
| `packaging/vr/` (repo root) | The .bat files, PowerShell scripts, README-FIRST.txt that go into the zip. |
| `tools/package_vr.py`, `tools/pack_vr_prompts.py` | Package / update-zip builder; Quest/Index prompt atlas packer. |

**Building on Windows** (Sly 2 does not build with MSVC):
```
:: LLVM/MinGW for Windows (ucrt, x86_64) from https://github.com/mstorsjo/llvm-mingw/releases, plus CMake and Ninja
set CANE_MINGW_ROOT=C:\llvm-mingw
cmake -S . -B build\windows-vr -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-toolchain.cmake -DCMAKE_BUILD_TYPE=Release ^
      -DCANE_BUILD_GAME=OFF -DCANE_BUILD_LAUNCHER=OFF -DCANE_BUILD_SLY3=OFF -DSLY2_VR=ON -DCANE_LTO=OFF
cmake --build build\windows-vr --target Sly2-Release
```
**The checkout path must have no spaces** (the top-level CMakeLists passes `sly_win32_compat.h` with `SHELL:-include`).
First configure downloads GLFW, SDL2, FAudio, XAudio2 and the OpenXR SDK 1.1.65, all pinned by hash. Test a code
change by copying the new `Sly2-Release.exe` over the installed one only after backing it up, or better: make an
update zip with `python tools/package_vr.py --build build\windows-vr --mingw %CANE_MINGW_ROOT% --version 0.1.x
--update-from Sly2-VR-0.1.0.zip --out .` and apply it with `Update.bat` (Rollback works then).
A normal launch must stay unchanged: every VR change is behind `SLY2N_VR` (compile) and `vr::Active()` (run time).
