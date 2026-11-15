SLY 2 VR (prototype) - READ ME FIRST
====================================

This is an early test build of a VR mod for SafeHouse's Sly 2 port: expect rough edges. Your normal SafeHouse game
and saves are never changed: VR has its own copy of your saves.

WHAT YOU NEED
  - Windows 10 or 11 PC with SafeHouse installed, and Sly 2 installed in it from your disc image.
  - A Meta Quest 3 (or another PC VR headset) streamed from the PC: Steam Link, Meta Quest Link (cable or Air Link),
    or Virtual Desktop. Whichever you use must be the active OpenXR runtime (below).

QUEST 3 WITH STEAM LINK
  1. PC: install Steam and SteamVR (free). Quest: install "Steam Link" from the Meta Horizon Store.
     Best: the PC wired to the router, the Quest on 5 GHz or 6 GHz Wi-Fi.
  2. Open Steam on the PC, then Steam Link on the Quest, and connect (a code pairs them the first time).
  3. Once: SteamVR Settings > OpenXR > "Set SteamVR as OpenXR Runtime". (The Meta Quest Link app often takes this
     over; the top of every log says which runtime was active.)
  4. Start a "Sly 2 VR" launcher from the Start menu (before putting the headset on, or from SteamVR's Desktop view).
  If you use Meta Quest Link instead: its app > Settings > General > "Set Meta Quest Link as active OpenXR runtime".

INSTALL
  1. Extract this zip anywhere (your Downloads folder is fine).
  2. Double-click Install.bat. It finds SafeHouse's Sly 2 (or asks for your portable SafeHouse folder), installs to
     %LOCALAPPDATA%\Programs\Sly2VR, copies your saves into a VR profile, and adds a "Sly 2 VR" Start menu folder
     and a desktop shortcut. You can delete the extracted folder afterwards.

FIRST TEST RUN (in this order: each step narrows down a problem if one comes up)
  1. Start the headset's PC link (Quest: Link / Air Link) so the headset shows the PC link home.
  2. Start "Sly 2 VR - Big screen". You should see the normal game on a large floating screen.
     If nothing appears in the headset: close it, run "Collect Logs" and send me the zip.
  3. Start "Sly 2 VR - Stereo (third person)". The game in 3D around you, the HUD on a panel in front of you.
  4. Start "Sly 2 VR - First run (check the hands)". First person as Sly, with his cane in your right hand.
     After the Sucker Punch logo, the title asks for a T-pose (once): stand up straight, face forward, hold your
     arms straight out to the sides and hold BOTH TRIGGERS for 2 seconds while staying still (a bar fills up).
     That sets your height, your forward direction and how your hands hold the controllers, for Sly, Bentley and
     Murray alike. B puts it off. Again any time: Options > VR Settings > Recalibrate, or "Sly 2 VR - Calibrate".
     This run also writes Sly's bones into the log so the hands can be fixed if they are wrong.
  5. Try each character (Sly, Bentley, Murray), the binocucom, a throw, a turret if you reach one, Bentley's darts,
     and the hands: the cane on your back, a pocket, the wristwatch, a pole, the paraglider (USING YOUR HANDS below).
  6. After any run: "Sly 2 VR - Collect logs" puts a zip on your desktop. Send it to me with what you saw.

CONTROLS (first person, Quest Touch)  - one rule everywhere: GRAB with the grip, USE with the trigger
  Left stick        move (where you look)       click: sprint on/off      hold the click: world stats
  Right stick       snap turn                   click: binocucom
  A                 jump
  B                 interact / grab: the button way of climbing; at a guard's pocket: HOLD it to steal (below)
  X                 uppercut (or swing the cane upward)
  Y                 hold: pause
  Right trigger     attack with the cane (or just swing it)
  Left trigger      use your gadget (the one picked on the wristwatch)
  Right grip        grab (the cane on your back, pockets, poles, ledges); anywhere else: sprint while held,
                    and in the air: the paraglider (jump, then the right grip; keep holding it)
  Left grip         grab (pockets, poles, ledges)
  The Menu button (three lines) belongs to Steam Link / Quest Link and is not used by the game.
  Bentley: darts on the right trigger, his swing on the right grip.

USING YOUR HANDS (first person)
  The cane      Reach behind your right shoulder (a tick in your hand) and grip: the cane goes on your back, and
                both hands are Sly's bare hands (no attacks). Reach there again and grip to draw it.
  Pickpocket    Sneak up behind a guard and HOLD B: while you hold it the cane can't attack. Bring the cane's
                hook to the pocket (your right hand ticks, then beats softly while it is in reach) and flick
                the cane: the loot is yours (a buzz). No prize card. Cane on your back: hold B, put your hand
                at the pocket, and flick it or grip. (VR Settings > Pickpocket: Grab = just grip at a pocket,
                no B; Button = the game's own B press.)
  Gadgets       Turn the inside of your right wrist toward your eyes: the wristwatch shows your gadgets. Point at
                one with your left hand and pull the left trigger. From then on the left trigger uses it.
  Climbing      Grip a pole, pipe, hook or ledge (where the blue sparkles are) with either hand. On a pole, pull
                down hand over hand to climb. On a ledge, keep gripping to hang; let go to drop; A climbs up.
  Paraglider    Jump, then hold the right grip. Raise both hands and grip with both, as if holding lines: pull the
                left hand down to turn left, the right to turn right, both down to slow down.
  Cutscenes (the Cairo intro and every other one) play from the game's own camera, with your head as the camera
  operator: look around freely, the black bars are gone, and a quick fade covers each camera cut.

VR SETTINGS (in the game: Options > VR Settings, from the pause menu or the title)
  Recalibrate (T-pose), Sly Size, Cane Size, Cane Turn X/Y/Z, Cane Shift X/Y/Z, Hands on/off, Gadget Menu
  (wristwatch or the Y button), Pickpocket (hold B, grab or button), Climbing (grab or button), Glider (hand lines or
  stick), Reset. Move the left stick left / right to change a value: it applies at once and is saved.

HELPERS (hold the LEFT stick click, then quickly press)
  A   reload Sly2VR.ini (after editing it while the game runs)
  B   recenter (the way you face becomes forward)
  X   fine-tune one character's hands: press once, the hands freeze where the game holds them; turn your hands to
      match; press again (saved for that character only; the T-pose redoes all of them)
  Y   height only: sit or stand as you'll play, press it: your eye height is saved and the world scaled to it
  Keyboard: F8 recenter, Ctrl+F8 reload, Shift+F8 hand calibration, Alt+F8 height calibration.
  The game pauses by itself when you take the headset off or open the Quest menu.

SETTINGS
  "Sly 2 VR - Settings" opens Sly2VR.ini in Notepad (it appears after the first VR start). Each setting has a comment.
  [default] applies to everyone; [sly], [bentley] and [murray] change it per character. Updates never replace it.
  mirror = left|eye|hud chooses what the desktop window shows (your left eye with the HUD, the eye only, the HUD only).

UPDATES
  I'll send small update zips (Sly2-VR-update-<version>.zip). Drag one onto "Update.bat" in the install folder (or
  drop it in the updates folder and run Update.bat). It checks every file, backs up what it replaces, and keeps your
  VR saves, settings and logs. "Rollback.bat" undoes the last update.

WHERE THINGS ARE (%LOCALAPPDATA%\Programs\Sly2VR)
  bin\        the game                 user\       your VR saves and settings (a copy of your normal Sly 2 profile)
  logs\       one log per run          backup\     what updates replaced (for Rollback)
  Sly2VR.ini  the VR settings          version.txt the installed version

KNOWN GAPS IN THIS BUILD
  - Hacking, the RC missions and the vault dial still show in 3D around you; use "Big screen" mode for those parts.
  - The T-pose is designed for standing play; seated play keeps your standing height for now (Y above re-does it).
  - Sly's gloves are one piece each: the fingers do not curl with your own.
  - Hand over hand climbing is Sly's only for now; Bentley and Murray grab and hang but climb with the stick.
  - Menus can't be clicked with the laser yet (the left stick and A); the wristwatch can.
  - Everything in USING YOUR HANDS is new in 0.1.2 and untested on a headset: tell me what feels off.
