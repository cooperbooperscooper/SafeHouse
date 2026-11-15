#pragma once

// (PC, VR: vr.h) The VR settings per character, from Sly2VR.ini (SLY2N_VR_INI, else <user dir>/Sly2VR.ini): built-in
// defaults, then the file's [default] section, then the playing character's ([sly], [bentley], [murray]); the
// SLY2N_VR_* environment variables (vr.h) override them all, for quick tests. The file is read at the start and again
// on a reload (vr.h: hold the left stick click + A, or Ctrl+F8); the calibrations write their results into it,
// keeping its other lines and comments. A missing file is written out with the defaults and comments the first time.

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace vrprofile
{
enum class Char { Default, Sly, Bentley, Murray };
const char* SzChar(Char c);   // the section's name: "default", "sly", "bentley", "murray"

// The motion controllers' physical controls (Index: X / Y are the left hand's A / B, Menu is the left trackpad
// squeezed, PadR the right one).
enum Ctl : int
{
    Ctl_A, Ctl_B, Ctl_X, Ctl_Y, Ctl_TriggerL, Ctl_TriggerR, Ctl_GripL, Ctl_GripR, Ctl_StickClickL, Ctl_StickClickR,
    Ctl_Menu, Ctl_PadR, Ctl_Max
};
const char* SzCtl(int ctl);   // the .ini key: "a", "trigger_right", ... (a long press: the key with "_hold")

// Besides the PS2 buttons (joy.h BTN_*, 16 bits): sprint_toggle, R1 latched by a tap until the left stick rests, a jump
// or another tap (tactical shooters' sprint; R1 held in the air would open the paraglider, so a jump ends it).
constexpr uint32_t kBtnSprintToggle = 0x10000;
// A press shorter than this is a control's tap, a longer one its hold (_hold keys).
constexpr double kDtHold = 0.5;

struct PROFILE
{
    float sEye = 25.0f;           // the eye above the character's centre, game units (first person)
    float scale = 100.0f;         // game units per metre of your movement (when no height calibration)
    float mpsSwing = 2.5f;        // a right-hand swing's speed, m/s
    glm::vec3 degHandR{0.0f};     // the right hand's turn on its grip, degrees (the cane turns with it)
    bool fHandL = false;          // degHandL set (else the right's mirror image)
    glm::vec3 degHandL{0.0f};
    // a hand's turn set for this character itself (its section's hand_rot / hand_rot_left, SLY2N_VR_HAND): it wins
    // over the T-pose's (FTPose); "auto" or none: the T-pose's when there is one, else the values above
    bool fHandOwnR = false, fHandOwnL = false;
    bool fForearm = true;         // the forearm drawn with each hand
    bool fShowHands = false;      // first person: the hands drawn (false: only what the character holds, Sly's cane)
    int iboneL = -1, iboneR = -1; // the hands' bones by vrbody's printed index (-1: found by themselves)
    uint32_t abtnCtl[Ctl_Max]{};  // each control's PS2 button (joy.h BTN_*, or kBtnSprintToggle; 0: none)
    uint32_t abtnHold[Ctl_Max]{}; // its long press's (0: none: the control acts at once, without waiting)
    uint16_t btnSwing = 0, btnSwingUp = 0;   // what a swing / an upward swing presses (0: nothing)
    bool fCalibrated = false;     // (0.1.0's guided calibration; no longer used)
    // (0.1.2, first person) size, the cane in your grip, and the physical interactions (the VR Settings page)
    float slySize = 1.6f;         // you as a bigger (> 1) or smaller Sly: the world's scale and your eye height follow
    float caneSize = 0.9f;        // the cane drawn in your hand x this (its game reach unchanged)
    // (0.1.3) the bare hands' own tuning, both hands, in the grip's axes (the left mirrored): size (about the wrist), a
    // turn (degrees) and a shift (centimetres) on top of the hand calibration
    float handSize = 0.8f;
    // (0.1.6) the character's own hands relative to Sly's (the player's tuned size is Sly's; Bentley's and Murray's gloves
    // are larger next to him): hand_scale in a section, times hand_size
    float handCharScale = 1.0f;
    // (0.1.7) turning with the right stick: smooth (the default), or snap by 30 degrees; the smooth turn's speed, degrees a second
    bool fSmoothTurn = true;
    float degsTurn = 120.0f;
    bool fCaneOnHand = true;      // cane_fit: the cane rides the tuned hand (true), or the raw grip (false, 0.1.2's fit)
    glm::vec3 degHand{0.0f};
    glm::vec3 cmHand{0.0f};
    glm::vec3 degCane{0.0f};      // the cane's turn in your grip, degrees about the grip's x (palm), y (back), z (fist)
    glm::vec3 cmCane{0.0f};       // its shift in your grip, centimetres along the same axes
    int nGadgetMenu = 0;          // 0: the wristwatch (turn your right wrist to you, point, left trigger); 1: Y (Select)
    bool fGrabSteal = true;       // pockets picked by gripping at them (B still works); false: B only
    bool fGrabClimb = true;       // poles, pipes, hooks and ledges grabbed by gripping at them; false: B only
    bool fGlideLines = true;      // the paraglider steered by your raised hands as on lines; false: the stick
};
// The built-in profile of a character (before Sly2VR.ini): the VR Settings page's reset.
PROFILE ProfileBuiltInOf(Char c);

// Read (again) from the file. False: no file could be read or written (the built-in defaults are used).
bool FLoad();
const std::string& StrPath();
// The resolved profile of a character (built-in, [default], its section, the environment).
const PROFILE& Profile(Char c);
// Your eye height above the floor in metres from the height calibration ([default] player_eye_height), 0: none.
float MPlayerEye();
// The T-pose calibration ([default] tpose_left / tpose_right, vr.h): each grip's turn in your body's axes (right,
// up, back) while you stood with your arms straight out to the sides. False: not done yet.
bool FTPose(glm::mat3* pmatL, glm::mat3* pmatR);
// A [default] setting as written (lower case), else strDefault.
std::string StrSetting(const char* szKey, const char* szDefault);
// The file sets this key in this section ("default" or a character's) itself.
bool FHasKey(const char* szSection, const char* szKey);
// Written into the file (and the profiles resolved again): a key of a section ("default" or a character's).
bool FSave(const char* szSection, const char* szKey, const std::string& strValue);
// A PS2 button's name ("cross", ..., "none") and back (0: none or unknown, *pf false when unknown).
const char* SzButton(uint32_t btn);
uint32_t BtnFromSz(const std::string& str, bool* pf);
}  // namespace vrprofile
