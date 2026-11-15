# Sly 2 VR 0.1.2: tuning and troubleshooting guide

For a Claude session on the player's Windows PC. Read `Sly2-VR-HANDOFF.md` first (install layout, ground rules, how a run
starts, reading logs). This file covers **0.1.2's new first-person interactions**: what each one does, what is likely to
feel off, and exactly what to change.

The player uses a **Meta Quest 3 over Steam Link** (SteamVR is the OpenXR runtime). 0.1.2 was built and checked on a Mac
(all builds compile, the settings tests pass, the hand and cane detection was checked on the player's own disc), but
**none of the 0.1.2 interactions had been tried on a headset** before this PC. Expect to tune thresholds and angles.

## Ground rules (same as the handoff)

- Never edit or delete the player's saves (`%LOCALAPPDATA%\Programs\Sly2VR\user\`) or the SafeHouse install.
- Never hand-edit files in `bin\`.
- You may change `Sly2VR.ini` freely. Prefer the in-game **Options > VR Settings** page for the values it has: it saves to
  the same file.
- Code changes go back to the Mac session (see "Escalating" at the end). This PC has the built game, not the source.

## Where things are

| What | Where |
| --- | --- |
| Install | `%LOCALAPPDATA%\Programs\Sly2VR` |
| Settings | `%LOCALAPPDATA%\Programs\Sly2VR\Sly2VR.ini` (open with "Sly 2 VR - Settings") |
| Logs | `...\Sly2VR\logs\sly2vr-<date>-<mode>.log`, one per run, last 20 kept |
| Version | `...\Sly2VR\version.txt` (should say `0.1.2`) |
| Undo the update | `Rollback.bat` in the install folder |
| Controls sheet | `CONTROLS.txt` in the install folder |

**Reload settings while playing:** hold the left stick click and press A (keyboard: Ctrl+F8). Values changed on the VR
Settings page apply at once without a reload.

**Verbose log:** start **"Sly 2 VR - First run (check the hands)"**. It sets `SLY2N_VR_BONES=1`, which logs the bone
table, the hands chosen, the T-pose derivation and the cane's measurement. Then run **"Collect Logs"**.

## How the 0.1.2 interactions work (so you can reason about a report)

One rule everywhere: **grab with the grip, use with the trigger.**

### Hands
- Sly's, Bentley's and Murray's hands are drawn on the controllers by default (`show_hands = 1`).
- The hand bones are found from each model's **bind pose**, which is a T-pose: the bone at the end of each arm. For Sly's
  right hand, the bone the cane hangs from is used instead. Checked on this disc:

  | Character | Left | Right | Forearms |
  | --- | --- | --- | --- |
  | Sly | 7:29 | 7:36 | 7:69, 7:66 |
  | Bentley | 8:52 | 8:36 | 8:47, 8:33 |
  | Murray | 9:72 | 9:119 | 9:68, 9:113 |

  Here `7:36` means record 36 of object 7. The log's `vr hands:` table uses its own index numbers instead.
- The hand's rotation comes from the T-pose calibration combined with each model's bind pose.
- Gloves are one bone each, so the fingers cannot curl. This is a known limit, not a bug.

### The cane (Sly)
- It is placed by its own shape (measured from its vertices once per level):
  - the shaft runs through the middle of the controller's handle, along the fist;
  - the hook comes out above the thumb and curls forward;
  - the right glove is drawn around it exactly as Sly's animation holds it.
- On this disc: the hook is about 86 game units beyond the grip and the straight end about 61 below it.
- **Holster:** the right hand reaches behind the right shoulder (it ticks), then grips. The cane goes on the back,
  sword-style:
  - the grip end rests at the right shoulder;
  - the crook hangs down behind the back toward the left hip.

  Holstered means bare hands: no attack from the trigger, from swings or from X. Grip there again to draw it.

### Pickpocketing (Sly)
- A guard's pocket can be taken when:
  - the game itself would allow a steal (the guard is unalerted and Sly is inside its pickpocket volume, like the B prompt);
  - and a hand is within about 40 game units of the pocket, or the cane's hook within about 35.
- The hand ticks when it is in reach. Gripping takes the prize at once, with a strong buzz and then a weaker one.
- **No blue prize card**, either for the grip or for B.
- While a pocket is in reach, swings and the right-trigger attack are off, so a reach doesn't start a fight.

### Wristwatch (gadgets)
- It opens when the inside of the **right** wrist faces the eyes (within 40°, staying open up to 50°) and the hand is
  within 0.65 m of the head.
- It shows the character's owned gadgets. Point with the **left** hand's laser and pull the **left trigger** to pick one.
- The picked gadget goes into the game's L1 slot, and from then on the left trigger uses it.
- With `gadget_menu = button`, Y opens the game's own gadget menu instead.

### Climbing
- A hand within about 45 game units of a pole, pipe, hook, spire or ledge (the places with blue sparkles) is a grab zone,
  and it ticks.
- Gripping there presses the game's Circle, so the game attaches its own way.
- On a pole, pulling a gripping hand along the pole moves Sly the other way (hand over hand). Below about 0.3 m/s of hand
  speed the thumbstick works as usual. Hand over hand is Sly's only.
- On a ledge, keeping a grip held keeps Circle held. Letting go drops Sly. A climbs up.

### Paraglider
- Jump, then hold the right grip. Opening it is unchanged from 0.1.1.
- To steer by lines: both hands above head height minus 0.45 m, and both grips held.
  - The left hand lower turns left; the right hand lower turns right.
  - Both hands lower slow it down and flatten the glide.
- With `glide = stick`, only the stick steers.

### Grip timing (relevant to "stray sprint" or "glider opens and closes" reports)
- A grip press in a grab zone is a grab: R1 is never sent for that whole press.
- A press elsewhere sends its button (R1 on the right: sprint, or the glider in the air) **2 game steps (~33 ms) late**.
  If the hand reaches something grabbable in those 2 steps, the press becomes a grab instead.
- A squeeze while the sprint toggle is on ends the toggle, so the glider can open after running off a roof.

### Rumble
Ticks and buzzes come through an OpenXR vibration action, and the game's own rumble goes to both controllers. Both
honour the launcher's "Controller vibration" setting.

## Settings you can change without code

All of these are in `Sly2VR.ini`. **[default]** applies to everyone; **[sly] / [bentley] / [murray]** override per
character. Those with a menu row are also on **Options > VR Settings** (left/right changes them, saved at once).

| Key | Default | Menu row | What it does / when to change it |
| --- | --- | --- | --- |
| `show_hands` | 1 | Hands | 0 = only the cane (0.1.1 look). |
| `sly_size` | 1.1 | Sly Size (70-160%) | You as a bigger Sly: the world looks smaller and the eye is higher. World too big → raise it. |
| `cane_size` | 0.9 | Cane Size (50-130%) | The drawn cane's size only (its reach in the game is unchanged). |
| `cane_rot` | 0,0,0 | Cane Turn X/Y/Z (±90° in 5° steps) | The cane's turn in the grip, degrees about the grip's axes (see "Grip axes" below). |
| `cane_offset` | 0,0,0 | Cane Shift X/Y/Z (±10 cm) | The cane's shift in the grip, cm along the same axes. |
| `gadget_menu` | watch | Gadget Menu | `button` = Y opens the game's menu (the fallback if the watch misbehaves). |
| `pickpocket` | grab | Pickpocket | `button` = B only (no grab zones near guards). |
| `climb` | grab | Climbing | `button` = B only (no grab zones at poles and ledges). |
| `glide` | lines | Glider | `stick` = the stick only. |
| `swing_speed` | 2.5 | (none) | m/s for a cane swing to attack. Accidental attacks → raise it. |
| `hands` | auto | (none) | `left,right` indices from the `-Bones` log's `vr hands:` table, if the automatic choice is wrong. |
| `hand_rot` / `hand_rot_left` | auto | (none) | `auto` = from the T-pose. A value under a character's section wins for that character (the X helper writes one). |
| `tpose_left` / `tpose_right` | (set by T-pose) | Recalibrate | Delete both lines to be asked for the T-pose again. |
| `player_eye_height` | (set by T-pose) | Recalibrate | The player's eye height in metres. |

**Grip axes** (OpenXR's, right hand holding the controller upright in front of you):
- **X:** into the palm, toward your right.
- **Y:** toward you, back along the forearm.
- **Z:** down the fist toward the little finger. The hook points along −Z, out above the thumb.

So:
- `cane_rot = 0,0,20` rolls the cane 20° about its own shaft.
- `cane_rot = 20,0,0` tips the hook forward or back.
- `cane_offset = 0,0,-3` moves the cane 3 cm toward the hook.

Change one axis at a time and let the player judge.

**Reset:** VR Settings > Reset VR Settings writes the defaults above, in [default] and in any character section that has
them.

### Environment switches (set before running `bin\Sly2-Release.exe` by hand, or add to `scripts\launch.ps1`)

| Variable | Effect |
| --- | --- |
| `SLY2N_VR_WATCH_FLIP=1` | Turns the wristwatch's face 180°: try it if the watch reads upside down. |
| `SLY2N_VR_BONES=1` | Verbose hands, T-pose and cane log (the "First run" launcher sets it). |
| `SLY2N_VR_CALIBRATE=1` | Asks for the T-pose at the title even if one is saved (the "Calibrate" launcher sets it). |
| `SLY2N_VR_TPOSE=0` | Ignores the saved T-pose (falls back to `hand_rot`). |
| `SLY2N_VR_HANDS=l,r` | One-off hand bone indices. |
| `SLY2N_VR_AUTOPAUSE=0` | No pause when the headset loses focus. |

## Symptom → first thing to try

| Symptom | Try first | Then |
| --- | --- | --- |
| Cane floats beside the hand or at an angle | VR Settings: Cane Turn / Cane Shift, one axis at a time | Send the `-Bones` log: the `vr cane:` line should say "the shaft fitted on 55, 15.2 from the hand's record; the hook (the cane point) 85.8 out" (on this disc). Different numbers → escalate. |
| Cane upside down (hook below the hand) | Escalate with the `vr cane:` line | `cane_rot = 180,0,0` as a stopgap. |
| Hands on the wrong bones / a glove floats away | `-Bones` log, `vr hands:` table | `hands = L,R` under that character. |
| Hands turned wrong | Options > VR Settings > Recalibrate (T-pose) | Still wrong for one character → the X helper (hold the left stick click + X twice) for that character. The log line `T-pose: ... (hand_rot is used)` means the skeleton check failed: escalate. |
| Hands missing entirely | Check `show_hands = 1` | The log line `vr hands: not found` → escalate with the log. |
| World feels too big / small | VR Settings > Sly Size | Recalibrate the T-pose standing straight. |
| Can't holster / draw | Reach further behind the right shoulder (zone: up to 0.38 m right, 0.38 m below to 0.12 m above the eyes, 0.05-0.38 m behind) | Escalate with where the hand was. |
| Holster triggers by accident (e.g. scratching the ear) | Note how often | Escalate: the zone needs shrinking. |
| Watch never opens | Turn the inside of the right wrist fully to the eyes, hand close to the face | `gadget_menu = button` as a fallback; escalate. |
| Watch upside down | `SLY2N_VR_WATCH_FLIP=1` | |
| Watch sideways or tilted badly | Note the arm position when reading it | Escalate: the face orientation needs changing in code. |
| Watch laser misses the entries | Note which entry it lands on vs. aimed at | Escalate. |
| Pocket never ticks | Approach from behind until the game's own B prompt would show | `pickpocket = button`; escalate with distance notes. |
| Steals by accident | Note what the hand was doing | `pickpocket = button`. |
| Poles / ledges don't grab | The hand should tick at the pole | `climb = button` (B works); escalate. |
| Drops off ledges unexpectedly | Keep the grip squeezed (releasing it lets go) | `climb = button` and hold B instead. |
| Hand over hand climbs the wrong way | Note the pole (vertical, sloped, level) and the pull direction | Escalate (a sign to flip in code). |
| Stray sprint or glider when grabbing | Note the exact moment | Escalate (the grip timing above). |
| Glider won't open | Jump first, then a fresh right-grip squeeze in the air | If the sprint toggle was on, it should end on the squeeze; escalate if not. |
| Glider line-steering turns the wrong way | Note which hand down turned which way | `glide = stick`; escalate (a sign to flip). |
| No buzz at all | Check the launcher's Controller vibration setting | Escalate with the log's first `vr:` lines. |
| Swings attack when reaching for things | Raise `swing_speed` | |
| Settings change in the menu but "stick" | A character section ([sly] etc.) may hold the same key | The menu writes to that section when it has the key; check the file. |

## What to collect for any report

1. Reproduce once with "First run (check the hands)" if hands or the cane are involved.
2. Run "Collect Logs" and keep the zip from the desktop.
3. Write down: the character, the level and spot, what the player did with each hand, and what happened vs. what
   should have happened.
4. Note the `Sly2VR.ini` lines changed from the defaults.

## Escalating to the Mac session (code changes)

You can make code changes yourself from the player's private GitHub repo,
**<https://github.com/cooperbooperscooper/sly2-vr>** (branch `main` is the current release). Follow
`docs/VR-BUILD-WINDOWS.md` in it:
- log in once with `gh auth login` (the player approves the code in the browser);
- `gh repo clone cooperbooperscooper/sly2-vr C:\sly2vr\src`;
- build with LLVM-MinGW, package with `tools/package_vr.py`, install with Update.bat;
- push your fix on a `pc/<what>` branch and open a pull request (its body: what the player saw, what you changed, how
  you tested it); give the player the PR link for the Mac session, which replies with comments on it signed **[Mac]**.

`Sly2-VR-0.1.2-source.zip` holds the same source if the repo can't be reached. Without code access, send the report
above, plus the exact value you want changed if it's one of these. Each
entry gives the file, the constant, its default and its meaning.

**`vr.cpp`:**
- Grip press timing:
  - `kcstepGripOffer = 2`: steps a non-grab grip press waits before sending R1.
  - `kcstepGripPending = 5`: steps a grab-zone press stays takeable.
- Holster:
  - `kxHolster0/1, kyHolster0/1, kzHolster0/1`: the holster zone box in metres (right / up / back of the eyes).
  - `kmHolsterHyst = 0.05`: the zone's hysteresis.
  - `kxHolster, kyHolster, kzHolster = 0.17, -0.12, 0.18`: where the cane rests.
  - In `FHolsterLocal`, the vector `(-0.25, -1, 0.15)`: the direction toward the hook (down-left-back).
  - `kdegTorsoLag = 40`, `kdtTorsoFollow = 1.5`: how the body's yaw follows the head (no torso tracking).
- Watch:
  - `kdegWatchOpen = 40`, `kdegWatchStay = 50`: the opening angles.
  - `kmWatchReach = 0.65`: the hand's distance from the head.
  - `kdtWatchOpen = 0.15`, `kdtWatchClose = 0.3`: the debounce times.
  - `kdxWatch = 0.13`, `kdyWatch = 0.0975`: its size in metres.
  - `kposWatchGrip = (-0.03, 0.08, 0)`: its position in the grip; the face's rotation is in the same block.
- Paraglider:
  - `kmGlideBelowHead = 0.45`: how high the hands must be.
  - In `FGlideLines`, the turn divisor `0.25` m and the pull divisor `0.4` m.
- Haptics: the `Pulse(k, amplitude, seconds)` calls:
  - holster tick 0.25 / 0.02 s, holster toggle 0.7 / 0.06 s, grab-zone tick 0.2 / 0.02 s, watch pick 0.5 / 0.04 s.

**`vrinteract.cpp`:**
- Reaches: `kSReachPocket = 40`, `kSReachPocketHook = 35`, `kSReachClimb = 45` (game units; about 100 per metre).
- `kMpsClimbFull = 0.9`: hand speed in m/s for full climb speed. Hand over hand takes over the stick above 0.35 of that.
- Steal buzz: 0.8 / 0.08 s, then 0.45 / 0.05 s after `kCstepBuzz2 = 8` steps.

**`vrbody.cpp`:**
- `MeasureCane` / `MeasureCaneVertices`: the cane's shaft line and hook direction.
- `PxfArmEnd` (0.85 / 0.4) and `PxfForearm` (0.6): the hand and forearm search.
- `CharacterTPose` (15°): the A-pose correction.

**`menu.cpp`:** `AentVrSettings`, `StepVr`, and the step ranges in `StepVr`'s cases.

**`vrprofile.cpp`:** the defaults (`ProfileBuiltIn`), the ini template, and the layout 3 migration (`s_aoldkey`).

The Mac session can also run the game headless on the same disc to check the hands and the cane: `SLY2N_SKELDUMP=<file>`
with `--world 2 --headless --frames 200`.
