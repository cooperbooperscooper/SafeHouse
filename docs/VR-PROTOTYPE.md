# Sly 2 VR (OpenXR prototype)

An experimental VR mod for the Sly 2 port, tested on a Quest 3 over Steam Link (0.1.0, 0.1.1). 0.1.2 turns first
person physical, after Contractors' rules: grab with the grip, use with the trigger, things carried on the body.
Expect more rounds.

Players: the Windows package (`Sly2-VR-<version>.zip`) has `README-FIRST.txt` with the install steps, the first test
run's checklist, the controls and the helpers. This page is for building and changing the mod.

## What it does

| Mode | `SLY2N_VR=` | What you see |
| --- | --- | --- |
| Screen | `screen` | The normal game, HUD included, on a large screen floating about 3 m in front of you. Lowest risk. |
| Stereo | `stereo` | The world in real 3D, drawn once per eye from the game's camera plus your head movement. The HUD sits on a panel in front of you. |
| First person | `fp` | You are the character: your eyes at its head, only Sly's cane drawn in your right hand by default (`show_hands = 1` adds the hands), no body tracking. The left stick walks where you look, the right stick snap-turns 30°, swinging your right hand attacks (an upward swing: the uppercut). Every cutscene plays from the game's own camera with your head as the camera operator (no letterbox bars, a fade on each cut). |

A normal launch is unchanged: VR starts only when `SLY2N_VR` is set (the package's launchers set it), the way
CircuitLord's installers keep a game's normal launch flat and add a separate "Launch in VR". If anything fails (no
runtime, no headset), the game prints why and carries on in the window.

## Requirements

- **Windows** (or Linux with X11 / XWayland) and the OpenGL renderer. macOS has no OpenXR runtime for PC headsets.
- An OpenXR runtime: Meta Quest Link / Air Link, Virtual Desktop, SteamVR, WMR, or Monado / ALVR / WiVRn on Linux.
- **Meta Quest 3:** streamed from the PC (Link, Air Link, Virtual Desktop, Steam Link); its Touch Plus controllers
  use the Touch bindings. Standalone on the headset is not supported (it would be a separate Android port).

## Controls and settings

Each character has its own settings in **`Sly2VR.ini`** ([`vrprofile.cpp`](../Sly2-Release/vrprofile.cpp); the
package points `SLY2N_VR_INI` at its own copy, else `<user dir>/Sly2VR.ini`). The file is written with every key
documented the first time VR starts; updates never replace it. `[default]` applies to everyone, `[sly]`, `[bentley]`
and `[murray]` change it per character, and the `SLY2N_VR_*` variables override it for quick tests.

- **Buttons:** each physical control (`a b x y trigger_left trigger_right grip_left grip_right stick_click_left
  stick_click_right menu trackpad_right`) presses a PS2 button, `<control>_hold` its long press (0.5 s). Defaults
  (layout 3): A Cross, B Circle, right trigger Square, right grip R1 (it grabs where there is something to grab), X
  Triangle, Y Select only with `gadget_menu = button` (hold: Start, the pause), left trigger L1 (the one gadget
  button), left grip none (it grabs), left stick click `sprint_toggle` (hold: L3), right stick click R3, Menu none
  (Steam Link's). Bentley: darts (R1) on the trigger, his swing (Square) on the grip. An older file is migrated.
  On a Valve Index, x / y are the left A / B, menu and trackpad_right the trackpads squeezed.
- **Gestures (first person):** `swing` (default Square) and `swing_up` (Triangle; none for Bentley) at `swing_speed`
  m/s of the right hand.
- **First person:** `eye` (game units above the character's centre: Sly 25, Bentley 10, Murray 45, all guesses),
  `hands` (auto, or the bone indices from the `SLY2N_VR_BONES=1` printout), `hand_rot` / `hand_rot_left` (auto: the
  T-pose's; a character's own value wins), `show_hands`, `forearm`.
- **T-pose calibration:** asked for once at the title after the logo movie (`vr.cpp` NoteTitle; again with
  `SLY2N_VR_CALIBRATE=1` or Options > VR Settings > Recalibrate): arms out, both triggers held still 2 s. It saves each grip's
  turn in your body's axes (`tpose_left` / `tpose_right`, quaternions w,x,y,z), `tpose_span` and `player_eye_height`.
  Each character's hand turn in its own T-pose comes from its bind pose (`vrbody.cpp` CharacterTPose: the forearm
  turned level to its side, so an A-pose model works too); the grip's calibration is then grip^T x hand.
- **World scale:** `scale` (game units per metre, a guess), or the height calibration's `player_eye_height`; then
  `sly_size` (default 1.1: you as a slightly bigger Sly; your eye height follows so the floor stays put).
- **The cane** (first person, Sly): held by its own shape (`vrbody.cpp` MeasureCane: the shaft is the vertices' main
  axis, the hook the end that bends, the grip where his hand's record falls on the shaft): the shaft along the grip's
  -Z (little finger to thumb), the hook out over the thumb curling forward (the grip's -Y); `cane_size` (0.9),
  `cane_rot` (degrees about the grip's axes), `cane_offset` (cm). The right hand closes on it as the animation holds
  it. Holstered (grip behind the right shoulder, `vr.cpp` FRightHandAtHolster): across the back, both hands bare, no
  attacks.
- **The physical interactions** (`vrinteract.cpp`, at step time): `pickpocket = grab` (a hand or the cane's hook at a
  readied pocket, then a grip: PloTakePocket and the prize collected; no prize pose, so no prize card, also for B),
  `climb = grab` (a grip at one of the game's attach targets presses Circle; hand over hand on poles drives the stick;
  ledges held while a grip is), `gadget_menu = watch` (the right wrist's inside turned to the eyes shows the gadgets
  on a quad layer; the left laser and trigger pick one into the L1 slot), `glide = lines` (`po.cpp`: both hands raised
  and gripping steer the paraglider). Haptics through an OpenXR vibration action; the game's rumble reaches both
  controllers.
- **VR Settings page** (`menu.cpp` AentVrSettings, Options > VR Settings): those settings, the T-pose and a reset,
  changed with left / right and saved to `[default]` at once.
- **Desktop window:** `mirror = left` (your left eye with the HUD), `eye` or `hud`.

**Helpers** (hold the left stick click, then press): A reload the .ini, B recentre, X one character's hand fine-tuning
(press, match your hands to the frozen ones, press again: `hand_rot` saved in its section), Y height calibration (your eye height above the floor
saved; the world scaled so you see from the character's eye height). Tapped alone, the left stick click is its own
control (the sprint toggle).
Keyboard: F8 recentre, Ctrl+F8 reload, Shift+F8 hand, Alt+F8 height. **Auto-pause:** the pause menu opens when the
session loses focus (headset off, system menu); `SLY2N_VR_AUTOPAUSE=0` turns it off.

**Button prompts:** while the VR controllers are the last input, the game's prompts show the Quest Touch (or Index)
icon of the control your profile puts that button on, from `assets/prompts/prompts_vr.png` (Kenney Input Prompts
1.5A, CC0; `tools/pack_vr_prompts.py`). A keyboard or gamepad input switches them back. "Original PlayStation Icons"
turns it all off, as before.

## How it works (code map)

- [`vr.h`](../Sly2-Release/vr.h) / [`vr.cpp`](../Sly2-Release/vr.cpp): the OpenXR instance, session, swapchains,
  frame loop, actions (physical controls; Touch and Index bindings), the helpers, auto-pause, the aim ray and laser
  pointer, the HUD / screen panel, the desktop mirror. Without `-DSLY2_VR=ON` every function is an inline no-op.
- [`render.h`](../Sly2-Release/render.h) `VIEWOVERRIDE`: `RenderScene` draws an eye's view instead of the camera's; the
  renderer culls from each pass's own matrix; the second eye skips the render targets the first drew.
- [`game.cpp`](../Sly2-Release/game.cpp): who plays (the profile), the first-person eye camera, the per-eye draws, the
  HUD layer, the panel, the mirror; `xrWaitFrame` paces the frames and the steps' interpolation fills 72-120 Hz.
- [`vrbody.cpp`](../Sly2-Release/vrbody.cpp): first person's hands: the hand bones (fingers, forearm) moved onto the
  grips, every other bone shrunk to a point by the nearer hand, the held object (Sly's cane) carried by the right
  hand. The right hand: `Sly2VR.ini` hands, else Bentley's swing-hand record (`BENTLEY::pxf10B8`), else the bone
  nearest the held object (Sly), else a guess (Murray); the left: the mirror branch.
- [`po.cpp`](../Sly2-Release/po.cpp) `UpdatePadctl`: first person's stick turned by your head's heading.
  [`joy.cpp`](../Sly2-Release/joy.cpp): the controllers' buttons and sticks with the pad's, auto-pause, prompt switching.
- **Aiming** ([`vraim.h`](../Sly2-Release/vraim.h)): the right controller's ray (only once it has been used in the last
  15 s, so a controller left on the desk never takes over from a gamepad) aims Bentley's darts and Carmelita's shot
  ([`gang.cpp`](../Sly2-Release/gang.cpp), [`pccharacter.cpp`](../Sly2-Release/pccharacter.cpp)), throws
  ([`jtthrow.cpp`](../Sly2-Release/jtthrow.cpp), [`murraycarry.cpp`](../Sly2-Release/murraycarry.cpp): onto the floor
  under the thrower) and turrets ([`turret.cpp`](../Sly2-Release/turret.cpp): absolute pan / tilt in the turret's
  frame; stereo holds the attach camera still while aiming). The binocucom follows your head
  ([`cplcy.cpp`](../Sly2-Release/cplcy.cpp), first person). Each falls back to the game's own aim without a
  controller.
- **What you see** ([`cm.h`](../Sly2-Release/cm.h) `CmView`): photo jobs, spawners that wait until you look away, guard
  pursuit, laser culling, Carmelita's chase, the sound listener, and (first person) the climbing stick use your head's
  view instead of the hidden game camera. `CmView()` is `g_cm` itself in a normal launch.

## Building

```bash
export CANE_MINGW_ROOT=/path/to/llvm-mingw
cmake -S . -B build/windows-vr -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-toolchain.cmake -DCMAKE_BUILD_TYPE=Release \
      -DCANE_BUILD_GAME=OFF -DCANE_BUILD_LAUNCHER=OFF -DCANE_BUILD_SLY3=OFF -DSLY2_VR=ON
cmake --build build/windows-vr --target Sly2-Release
```

The OpenXR loader (1.1.65, pinned by hash) is fetched and linked statically. The checkout's path must not contain
spaces: the top-level CMakeLists.txt passes `sly_win32_compat.h` with `SHELL:-include`, which splits there (an
existing limitation of the Windows build, not the VR mod's; build through a space-free symlink if needed).

**Packaging** ([`tools/package_vr.py`](../tools/package_vr.py)): the Windows zip (the exe, the DLLs it needs found by
following imports, the scripts in [`packaging/vr/`](../packaging/vr), licences, `manifest.json` with every file's
SHA-256) into `~/Downloads`; `--update-from <previous zip>` makes a small update zip of the changed files, which the
package's `Update.bat` checks and applies (with a backup for `Rollback.bat`).

## Known gaps (the next things to fix)

1. **0.1.2's physical interactions are untested on a headset** (thresholds, axes, the watch's facing, hand over hand):
   the hands are found from the bind pose (checked headless on the disc: Sly 7:29 / 7:36, Bentley 8:52 / 8:36, Murray
   9:72 / 9:119); `SLY2N_SKELDUMP=<file>` with a headless `--world N` run prints every gang member's skeleton and the
   hands chosen; `SLY2N_VR_BONES=1` logs the hands and the cane's measure in VR.
2. **Hacking, the RC missions and the vault dial** still show in 3D around you: they belong on the floating screen.
3. **Binocucom zoom** isn't visible in the headset (the eyes keep the headset's field of view).
4. **Seated play** keeps the standing T-pose height. Gloves are one bone each: no finger curls. Hand over hand is
   Sly's only.
5. **The cane in your hand is visual:** hits come from the game's own attack (a swing presses its button).
6. Sly's shadow shrinks to his hands in first person; Advanced Input Mapping hasn't been checked with the controllers.

## Roadmap

- Done in 0.1.2: the cane holster, bare hands, grab pickpockets, grab climbing, the wristwatch gadget picker, glider
  lines, haptics, the VR Settings page.
- Next: physical throws (gadgets drawn from hip holsters), the powers on the watch (Shadow Power, Thief Reflexes, Time
  Rush) and the minigames and menus on the floating screen, menus clickable with the laser.
- A minimap watch (top-down view, the bottles and loot left, counts), the vault dial turned by hand.
- Comfort: fades through ledge / pole snaps, a vignette in fast moves, a fade in walls, turn options.
- Per-eye resolution (sharper on a Quest 3), 3D sound by your head (done for panning), Touch icons on the Controls page.
