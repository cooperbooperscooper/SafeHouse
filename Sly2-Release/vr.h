#pragma once

// (PC, prototype) VR through OpenXR: the headset shows the game, on Windows and Linux with the OpenGL renderer.
// Built only with -DSLY2_VR=ON (SLY2N_VR=1); otherwise every function here is an inline no-op and the game is
// unchanged. Started only when asked for, so a normal launch stays flat (as CircuitLord's installers keep the
// game's normal launch unmodded and add a separate "Launch in VR"):
//
//   SLY2N_VR=screen   the normal picture (the HUD included) on a large screen floating in front of you
//   SLY2N_VR=stereo   the world drawn once per eye from the game's camera plus your head, the HUD on a panel
//   SLY2N_VR=fp       first person: your eyes at the player's head, its body hidden, Sly's cane in your right hand;
//                     the left stick walks where you look, the right stick snap-turns, a swing of the right hand attacks;
//                     only the player's hands are drawn, on the controllers (vrbody.h), no body tracking
//
// The motion controllers (OpenXR actions; suggested bindings for Meta Quest Touch, including the Quest 3's, and the
// Valve Index) press the PS2 buttons alongside the gamepad and keyboard (ReadControllers).
//
// Tuning (environment, prototype only; see docs/VR-PROTOTYPE.md):
//   SLY2N_VR_SCALE=units per metre of head movement and eye spacing (default 100: a guess at the world's scale)
//   SLY2N_VR_LEVEL=0  keep the game camera's pitch and roll (default 1: only its yaw, the horizon kept level)
//   SLY2N_VR_SCREEN=metres to the screen / HUD panel (default 3 in screen mode, 1.5 for the stereo HUD)
//   SLY2N_VR_EYE=the eye's height above the player's centre, in metres (first person; default 0.25)
//   SLY2N_VR_SWING=the right hand's speed in m/s that counts as a swing (first person; default 2.5)
//   SLY2N_VR_HAND=rx,ry,rz degrees: the right hand's turn on its grip, the left mirrored (first person; default
//                 0,0,0: tune it by eye; the cane turns with the hand)
// Per character (eye height, hands, button layout, gestures): Sly2VR.ini (vrprofile.h).
// Testing helpers: hold the left thumbstick click and press A (reload Sly2VR.ini), B (recentre), X (hand calibration:
// press once, match your hands to Sly's, press again) or Y (height calibration); tapped alone it is its own button.
// Keyboard: F8 recentres, Ctrl+F8 reloads, Shift+F8 the hand calibration, Alt+F8 the height calibration.
// Auto-pause: the game pauses when the headset loses focus (taken off, the system menu); SLY2N_VR_AUTOPAUSE=0: not.

#include "vrprofile.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

struct GLFWwindow;
struct CAMERA;
struct VIEWOVERRIDE;

namespace vr
{
enum class Mode { Off, Screen, Stereo, FirstPerson };

#if SLY2N_VR
// Before the window exists (Linux: GLFW is asked for X11, which OpenXR's OpenGL binding needs).
void PreInit();
// After the GL context is current. Starts only when SLY2N_VR asks; false (and the game stays flat) on any failure.
bool Start(GLFWwindow* window);
void Stop();
// A session was created (the headset may not be showing yet).
bool Active();
// The headset's session is running: xrWaitFrame paces the frames (the frame-rate cap's pacer stands aside).
bool FPacesFrames();
Mode CurrentMode();
// Stereo or first person: the world drawn per eye.
inline bool FEyes() { return CurrentMode() == Mode::Stereo || CurrentMode() == Mode::FirstPerson; }
inline bool FFirstPerson() { return CurrentMode() == Mode::FirstPerson; }
// First person whose eyes are at the player's head this frame (game.cpp: not in a cutscene or scripted shot, and the
// player a gang member; else the game camera's view). Head-relative walking, snap turns, swings and the binocucom by
// the head follow this, not the mode.
void SetFirstPersonEyes(bool f);
bool FFirstPersonEyes();
// A camera cut or a switch of view (game.cpp): the eyes blink (black, then a 0.2 s fade in), drawn by DrawEyeFade
// into each eye after its scene.
void BlinkView();
void DrawEyeFade();

// Each step (joy.cpp UpdateJoy): the controllers' PS2 buttons (joy.h BTN_*) ORed into *pgrf; their sticks replace the
// pad's where they are pushed further. First person: the right stick snap-turns (not given to the game) and a swing
// of the right hand presses Square.
// fMenu: a menu has the pad, or the ThiefNet shop or the safe house's menu is up (no snap turns or swings then).
void ReadControllers(uint16_t* pgrf, float* pxl, float* pyl, float* pxr, float* pyr, bool fMenu);
// First person: the world heading (radians, atan2(y, x) as po.cpp's camera pan) the left stick's forward points at:
// where the head looks. False (unchanged) otherwise.
bool FFirstPersonPan(float* pradPan);
// First person: the body's world heading (radians); the first call takes radCamPan (the game camera's), snap turns
// change it after.
float RadBodyYaw(float radCamPan);
// The head as last drawn (stereo or first person: the two eyes' middle and the left eye's turn, in the world): its
// position, the game camera's rows (cm.h: x forward, y left, z up), the tangents of the two eyes' combined half
// fields of view across and up, and a serial that changes with each new view. False until the eyes were drawn.
bool FHeadView(glm::vec3* ppos, glm::mat3* pmatRows, float* ptanX, float* ptanY, uint64_t* pserial);
// The aim (stereo or first person, motion controllers): hand k's (0 left, 1 right) aim ray in the world as last
// drawn: its origin and unit direction (OpenXR's aim pose: -Z along the pointing). False when not tracked.
bool FAimRay(int k, glm::vec3* ppos, glm::vec3* pdir);
// An aim system aimed with the controller this step (vraim.h ShowVrAim): the pointer is drawn from posFrom to posTo
// for a moment.
void NoteAimUsed(const glm::vec3& posFrom, const glm::vec3& posTo);
// The pointer into the scene target for one eye (after its RenderScene, before SubmitEye), while an aim system has
// used the controller lately; depth tested against the scene.
void DrawAimPointer(const VIEWOVERRIDE& eye);
// Each displayed frame (game.cpp): who is playing (the profile used) and its centre's height above its floor in game
// units (0: unknown); with a height calibration the world's scale follows from it.
// fCanCalibrate: first person with the player's hands, no cutscene or menu: the T-pose may be asked for here too (when
// the title was skipped).
void SetCharacter(vrprofile::Char c, float sCenterAboveFloor, bool fCanCalibrate = false);
// The T-pose calibration (first person): asked for once, at the title after the logo movie (fTitle: the title is up
// and no movie plays), when Sly2VR.ini has none yet (or always with SLY2N_VR_CALIBRATE=1, the Calibrate shortcut).
// Stand straight, face forward, arms out to the sides, both triggers held still for kDtTPose seconds: your eye height,
// forward and how you hold the controllers are saved, and every character's hands follow from its own skeleton
// (SetCharacterTPose). B puts it off. Nothing reaches the game from the controllers meanwhile.
constexpr double kDtTPose = 2.0;
void NoteTitle(bool fTitle);
void RequestTPose();      // (the options menu's Recalibrate VR)
// The playing character's hands in its own T-pose (vrbody.h: its bind pose with each forearm turned out level to its
// side), each in the character's body axes (right, up, back); null: unknown (the hand_rot settings are used).
void SetCharacterTPose(const glm::mat3* pamat2);
// The calibration's instructions to draw over the HUD layer this frame (vrui.h) and its progress (0..1, -1: none);
// false: none.
bool FGuidedMessage(std::vector<std::string>* palines, float* puProgress);
vrprofile::Char CharacterCur();
const vrprofile::PROFILE& ProfileCur();
// The hand calibration runs (first person): vrbody.h leaves the hands where the animation holds them and offers their
// world matrices (k 0 left, 1 right) for the capture.
bool FHandCalibrating();
void OfferHandPose(int k, const glm::mat4& matHandWorld);
// Each step (joy.cpp): Start to press for the auto-pause (fMenuUp: a menu already has the pad: nothing).
uint16_t GrfAutoPause(bool fMenuUp);
// The button prompts: the Valve Index's controllers are the ones in use (else the Touch's); the physical control
// (vrprofile.h Ctl_*) the playing character's profile puts a PS2 button on (-1: none).
bool FIndexControllers();
int CtlForButton(uint16_t btn);
// The prompts show the VR controllers' icons: a VR control or stick was used after the last keyboard or gamepad input
// (joy.cpp NoteOtherInput: a newly pressed key or pad button, or a stick pushed past half).
bool FPromptsVr();
void NoteOtherInput();
// First person: the eye's height above the player's centre, in world units.
float SEyeHeight();
// Hand k's (0 left, 1 right) grip in the world for the first-person camera cam (position and turn, no scale; the
// hand's calibration turn included). False when the hand is not tracked.
bool FHandWorld(int k, const CAMERA& cam, glm::mat4* pmat);

// ---- (0.1.2) the physical interactions. vr.cpp reads the controllers and owns these states; vrinteract.h acts on the
// game at step time (pockets, climbing, the gadget picked); vrbody.h draws the hands and the cane. "Grip" is the
// controller's grip button (squeeze); grab with the grip, use with the trigger, everywhere.
// Hand k's (0 left, 1 right) controller buzzes: amplitude 0..1 for seconds (0: the runtime's shortest), at frequency Hz
// (0: the runtime's choice); amplitude 0 stops it. Nothing with the launcher's Controller vibration off.
void Pulse(int k, float amplitude, float seconds, float frequency = 0.0f);
// The cane is in the right hand (else holstered behind the right shoulder, both hands bare: no attacks). Toggled by
// gripping with the right hand behind the right shoulder (a tick on reaching it, a buzz on the draw / holster).
bool FCaneDrawn();
void SetCaneDrawn(bool f);
// Hand k's grip in the world, no calibration turn and no scale: columns +X (the palm's normal: into the right palm, out
// of the left), +Y, +Z (-Z along the fist's tube, little finger to thumb), the origin the palm's centre in the handle.
// FGripWorld for this frame's first-person camera cam (the draw); FGripWorldLast for the camera the eyes were last drawn
// from (step time). False: the hand is not tracked (or not first person at the head).
bool FGripWorld(int k, const CAMERA& cam, glm::mat4* pmat);
bool FGripWorldLast(int k, glm::mat4* pmat);
// Hand k's velocity in the world (units / s, as of the last read), and in your own space (m / s, Y up).
bool FGripVelocityLast(int k, glm::vec3* pvelWorld, glm::vec3* pvelLocal);
// The holster behind the right shoulder in the world for cam: the origin where the cane's grip point rests, -Z along the
// cane toward its hook (worn like a sword: the crook down behind the back toward the left hip), +Y toward your front
// (the crook, on the cane's -Y side, curls away behind you). False: not first person or no head pose.
bool FHolsterWorld(const CAMERA& cam, glm::mat4* pmat);
// Game units per metre of your movement this frame (the scale with Sly's size).
float UnitsPerMetre();
// The grips: held now; a new press (TakeGripPress takes it once: it then presses nothing else until let go).
bool FGripHeld(int k);
bool FTakeGripPress(int k);
// (0.1.6) Bentley's aim held (the left trigger, first person): his darts are ready without the binocucom.
bool FAimHeld();
// (vrinteract, each step) Hand k is at something it can grab (a pocket, a climbable): its grip is a grab (not its button),
// the swing gestures are off, a tick on entering. SetPocketNear: a pocket is in reach of either hand (no attacks: no
// swings, no right trigger, while it is).
void SetGrabZone(int k, bool f);
void SetPocketNear(bool f);
// (pickpocket = hold, Sly) vrinteract, each step: a pocket the game would pick now (Sly inside a guard's pickpocket
// volume). There, B held is the steal stance instead of Circle: no swing, no attack, no uppercut while it is, and
// vrinteract steals when the cane's hook (or a bare hand) at the pocket is flicked (or the hand grips).
void SetPocketAvailable(bool f);
bool FStealStance();
// (vrinteract) PS2 buttons (joy.h BTN_*) pressed for csteps steps from the next read (a tap); held: pressed while asked
// each step (cstep 1 every step).
void RequestButtons(uint16_t grf, int csteps);
// (vrinteract) the left stick climbing hand over hand (-1..1; y > 0 up the pole), from the next read; fSet false: none.
void SetClimbStick(bool fSet, float x, float y);
// The paraglider's lines: both hands raised and gripping: turn (-1 left .. 1 right) and pull (0 .. 1: both down, brake).
// False: not held as lines (the stick steers).
bool FGlideLines(float* pturn, float* ppull);
// The wristwatch on the right wrist (the gadget picker, gadget_menu = watch): open while the inside of the right wrist
// faces your eyes. vrinteract sets its entries (names, the one in use, -1 none); the left hand's laser points at one
// (FWatchState's iHover, -1 none) and the left trigger picks it (TakeWatchPick, -1 none: taken once).
bool FWatchOpen();
void SetWatchEntries(const std::vector<std::string>& astr, int iCur);
bool FWatchState(std::vector<std::string>* pastr, int* piCur, int* piHover);
int TakeWatchPick();
// The watch's picture: the scene target cleared for it (drawn next, vrui.h DrawVrWatch), then onto its layer on the
// wrist (EndFrame shows it while open).
void BeginWatchLayer();
void SubmitWatch();
// (0.1.11) The binocucom on the left thigh (binocucom = thigh): the left grip at the pouch draws it into the left hand
// (a tick on reaching it, a buzz on the draw; let go, it goes back); raised to your eyes, the game's binocucom opens
// (R3 tapped for you), lowered it closes; twisted like a ring (clockwise as you look through it) it zooms in. vrinteract
// gives the game's binocucom each step (SetRecon: ACTOR nF04, nF20, the zoom 0..1, the fields of view zoomed out and
// now, radians); cplcy takes the twist's zoom (DuBinocTwist: the change since it last asked, 0..1 of the whole).
void SetRecon(int nState, int nView, float uZoom, float radFovFar, float radFov);
bool FBinocDrawn();
float DuBinocTwist();
// The binocucom's lens (your own binocucom view open, first person): its zoomed picture drawn once more from your eyes'
// middle (FLensView: false, none this frame), its round mask and copy (SubmitLens), shown before your eyes as a picture
// in picture. FLensDark: the eyes drawn dark this frame instead (DrawEyeDark), only the lens (binoc_view). FLensTan:
// tan of half its angle (CmView: what you can see is what the lens shows). DrawBinoc: the binocucom itself, per eye.
bool FLensView(VIEWOVERRIDE* pview);
void SubmitLens();
bool FLensDark();
bool FLensTan(float* ptan);
void DrawEyeDark();
void DrawBinoc(const VIEWOVERRIDE& eye);

// Once per displayed frame, before drawing: waits for the headset's timing and reads where the eyes are. True when
// this frame is to be drawn for the headset (else draw only the window, as usual). EndFrame must follow every
// BeginFrame, after the window's present.
bool BeginFrame(GLFWwindow* window);
// Stereo: eye k's (0 left, 1 right) view from the game's camera, and its image filled from the scene target.
bool EyeView(int k, const CAMERA& cam, VIEWOVERRIDE* pview);
void SubmitEye(int k);
// Stereo: the scene target cleared to transparent for the HUD's own layer (drawn next, then SubmitPanel).
void BeginHudLayer();
// The scene target as it is now onto the floating panel (screen mode: the whole picture; stereo / first person: the
// HUD, over the world). fScreen: this frame shows it as the big opaque screen instead, alone (a movie, a cutscene).
void SubmitPanel(bool fScreen = false);
// The desktop window's picture (instead of GL::Present) while the eyes are drawn: the left eye cropped to the window,
// the HUD over it (Sly2VR.ini mirror = left / eye / hud; SLY2N_VR_MIRROR). False: present as usual.
bool FPresentMirror();
void EndFrame();
#else
inline void PreInit() {}
inline bool Start(GLFWwindow*) { return false; }
inline void Stop() {}
inline bool Active() { return false; }
inline bool FPacesFrames() { return false; }
inline Mode CurrentMode() { return Mode::Off; }
inline bool FEyes() { return false; }
inline bool FFirstPerson() { return false; }
inline void ReadControllers(uint16_t*, float*, float*, float*, float*, bool) {}
inline bool FFirstPersonPan(float*) { return false; }
inline void SetFirstPersonEyes(bool) {}
inline bool FFirstPersonEyes() { return false; }
inline void BlinkView() {}
inline void DrawEyeFade() {}
inline float RadBodyYaw(float radCamPan) { return radCamPan; }
inline float SEyeHeight() { return 0.0f; }
inline bool FHeadView(glm::vec3*, glm::mat3*, float*, float*, uint64_t*) { return false; }
inline bool FAimRay(int, glm::vec3*, glm::vec3*) { return false; }
inline void NoteAimUsed(const glm::vec3&, const glm::vec3&) {}
inline void DrawAimPointer(const VIEWOVERRIDE&) {}
inline bool FHandWorld(int, const CAMERA&, glm::mat4*) { return false; }
inline void SetCharacter(vrprofile::Char, float, bool = false) {}
inline void NoteTitle(bool) {}
inline void RequestTPose() {}
inline void SetCharacterTPose(const glm::mat3*) {}
inline bool FGuidedMessage(std::vector<std::string>*, float*) { return false; }
inline vrprofile::Char CharacterCur() { return vrprofile::Char::Sly; }
inline const vrprofile::PROFILE& ProfileCur() { return vrprofile::Profile(vrprofile::Char::Sly); }
inline bool FHandCalibrating() { return false; }
inline void OfferHandPose(int, const glm::mat4&) {}
inline uint16_t GrfAutoPause(bool) { return 0; }
inline bool FIndexControllers() { return false; }
inline int CtlForButton(uint16_t) { return -1; }
inline bool FPromptsVr() { return false; }
inline void NoteOtherInput() {}
inline void Pulse(int, float, float, float = 0.0f) {}
inline bool FCaneDrawn() { return true; }
inline void SetCaneDrawn(bool) {}
inline bool FGripWorld(int, const CAMERA&, glm::mat4*) { return false; }
inline bool FGripWorldLast(int, glm::mat4*) { return false; }
inline bool FGripVelocityLast(int, glm::vec3*, glm::vec3*) { return false; }
inline bool FHolsterWorld(const CAMERA&, glm::mat4*) { return false; }
inline float UnitsPerMetre() { return 100.0f; }
inline bool FGripHeld(int) { return false; }
inline bool FAimHeld() { return false; }
inline bool FTakeGripPress(int) { return false; }
inline void SetGrabZone(int, bool) {}
inline void SetPocketNear(bool) {}
inline void SetPocketAvailable(bool) {}
inline bool FStealStance() { return false; }
inline void RequestButtons(uint16_t, int) {}
inline void SetClimbStick(bool, float, float) {}
inline bool FGlideLines(float*, float*) { return false; }
inline bool FWatchOpen() { return false; }
inline void SetWatchEntries(const std::vector<std::string>&, int) {}
inline bool FWatchState(std::vector<std::string>*, int*, int*) { return false; }
inline int TakeWatchPick() { return -1; }
inline void BeginWatchLayer() {}
inline void SubmitWatch() {}
inline void SetRecon(int, int, float, float, float) {}
inline bool FBinocDrawn() { return false; }
inline float DuBinocTwist() { return 0.0f; }
inline bool FLensView(VIEWOVERRIDE*) { return false; }
inline void SubmitLens() {}
inline bool FLensDark() { return false; }
inline bool FLensTan(float*) { return false; }
inline void DrawEyeDark() {}
inline void DrawBinoc(const VIEWOVERRIDE&) {}
inline bool BeginFrame(GLFWwindow*) { return false; }
inline bool EyeView(int, const CAMERA&, VIEWOVERRIDE*) { return false; }
inline void SubmitEye(int) {}
inline void BeginHudLayer() {}
inline void SubmitPanel(bool = false) {}
inline bool FPresentMirror() { return false; }
inline void EndFrame() {}
#endif
}  // namespace vr
