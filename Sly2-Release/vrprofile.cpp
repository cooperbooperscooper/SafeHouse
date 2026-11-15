// (PC, VR) The VR settings per character, from Sly2VR.ini (vrprofile.h).
#include "vrprofile.h"
#include "joy.h"
#include "../platform/platform.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

namespace vrprofile
{
namespace
{
constexpr const char* s_aszChar[] = {"default", "sly", "bentley", "murray"};
constexpr const char* s_aszCtl[Ctl_Max] = {"a", "b", "x", "y", "trigger_left", "trigger_right", "grip_left", "grip_right",
                                           "stick_click_left", "stick_click_right", "menu", "trackpad_right"};
struct BTNNAME { const char* sz; uint32_t btn; };
constexpr BTNNAME s_abtnname[] = {
    {"none", 0}, {"cross", BTN_Cross}, {"circle", BTN_Circle}, {"square", BTN_Square}, {"triangle", BTN_Triangle},
    {"l1", BTN_L1}, {"r1", BTN_R1}, {"l2", BTN_L2}, {"r2", BTN_R2}, {"l3", BTN_L3}, {"r3", BTN_R3},
    {"start", BTN_Start}, {"select", BTN_Select}, {"up", BTN_Up}, {"down", BTN_Down}, {"left", BTN_Left}, {"right", BTN_Right},
    {"sprint_toggle", kBtnSprintToggle}};
// The layout's version: 2 moved sprint to the left stick click, the pause off the Menu button (Steam Link's) onto a
// held Y, world stats and gadget 3 onto the stick clicks held. 3 (0.1.2) made the interactions physical: the hands
// shown, one gadget button (the left trigger, the gadget picked on the wristwatch), the grips grab. An older file's
// untouched layout keys are moved on. 4 (0.1.3): pickpocketing by holding B and flicking the cane's hook.
constexpr int kLayout = 4;
struct OLDKEY { int layout; const char* key; const char* szOld; const char* szNew; };
constexpr OLDKEY s_aoldkey[] = {
    {2, "stick_click_left", "l3", "sprint_toggle"}, {2, "menu", "start", "none"},
    {3, "grip_left", "l2", "none"}, {3, "stick_click_right_hold", "r2", "none"}, {3, "trackpad_right", "r2", "none"},
    {3, "show_hands", "0", "1"}, {4, "pickpocket", "grab", "hold"}};
constexpr OLDKEY s_anewkey[] = {   // (keys an older file lacks)
    {2, "y_hold", nullptr, "start"}, {2, "stick_click_left_hold", nullptr, "l3"}, {2, "stick_click_right_hold", nullptr, "none"}};

// The file as written the first time (and the documentation of every key).
constexpr const char* s_szDefaultFile = R"(; Sly2VR.ini: the Sly 2 VR mod's settings. Updates never replace this file.
; [default] applies to every character; [sly], [bentley] and [murray] change it for that character only.
; Reload while playing: hold the left thumbstick click and press A (keyboard: Ctrl+F8).
;
; Calibration (both write their result here):
;   T-pose (the main one): asked for once at the title, after the Sucker Punch logo. Stand up straight, face
;         forward, arms straight out to the sides, and hold both triggers for 2 seconds. It saves your eye height
;         and how you hold the controllers (tpose_left / tpose_right below), for every character. Again any time:
;         Options > VR Settings > Recalibrate (T-pose), or the "Calibrate" shortcut.
; Most of the settings below are on that VR Settings page too, changed live and saved here.
;   hand (fine-tuning one character): hold the left thumbstick click and press X (Shift+F8). The hands freeze where
;         the game holds them: turn your hands to match them, then press it again. It saves hand_rot in that
;         character's section, which wins over the T-pose (hand_rot = auto there goes back to the T-pose).
;   height: sit or stand as you will play, hold the left thumbstick click and press Y (Alt+F8). Your eye height is
;         saved as player_eye_height and the world is scaled so you see from the character's eye height.
; Recenter: hold the left thumbstick click and press B (F8).
;
; Buttons: each control presses a PS2 button: cross circle square triangle l1 r1 l2 r2 l3 r3 start select
; up down left right, sprint_toggle (R1 kept on by a tap until you stop moving, jump or tap again), or none.
; A key with _hold (y_hold, ...) is the control's long press (half a second): a tap then acts when you let go.
; On a Valve Index, x / y are the left hand's A / B, menu is the left trackpad squeezed and trackpad_right the right one.
; The Menu button (three lines) is Steam Link's and Quest Link's own menu: better left as none.

[default]
player_eye_height = 0     ; your eye height in metres, set by the height calibration (0: not calibrated, use scale)
mirror = left             ; the desktop window: left (your left eye + the HUD), eye (the eye only), hud (the HUD only)
scale = 100               ; game units per metre of your own movement, when not calibrated (a guess)
eye = 25                  ; first person: the eye's height above the character's centre, game units (a guess)
hand_rot = auto           ; the right hand's turn on the controller: auto (the T-pose's), or degrees x,y,z
; hand_rot_left = 0,0,0   ; the left hand's (default: the right's mirror image)
show_hands = 1            ; first person: 1 shows the character's hands on your controllers, 0 only the cane
sly_size = 1.5            ; first person: you as a bigger (more than 1) or smaller Sly; the world and your height follow
hand_size = 0.5           ; the bare hands drawn x this (about the wrist; also on the VR Settings page, in the pause menu too)
turn = smooth             ; the right stick turns you: smooth, or snap (30 degrees a flick)
turn_speed = 120          ; the smooth turn's speed at full stick, degrees a second
cane_fit = hand           ; hand: the cane rides your tuned hand (the glove and the cane stay together); grip: the raw controller grip
hand_turn = 0,-5,-10      ; the hands' turn on your grips, degrees x,y,z (the left mirrored)
hand_offset = 7,1,0       ; the hands' shift on your grips, centimetres x,y,z (the left mirrored)
cane_size = 0.9           ; the cane drawn in your hand x this (its reach in the game is unchanged)
cane_rot = -30,-5,-40     ; the cane's turn in your grip, degrees x,y,z (also on the VR Settings page)
cane_offset = -3,-1,-4    ; the cane's shift in your grip, centimetres x,y,z
gadget_menu = watch       ; watch: turn your right wrist to you, point with the left hand, pull the left trigger; button: Y
pickpocket = hold         ; hold: at a pocket hold B (the cane can't attack), bring its hook there, flick the cane
;                           (grab: reach for the pocket and grip, no B needed; button: B only, the game's way)
climb = grab              ; grab: grip a pole, pipe, hook or ledge with a hand to grab it; button: B only
glide = lines             ; lines: steer the paraglider with both hands raised and gripping; stick: the left stick
forearm = 1               ; with show_hands = 1: a bit of forearm with each hand (0: the hand only)
hands = auto              ; the hand bones: auto, or left,right indices from the SLY2N_VR_BONES=1 printout
swing_speed = 2.5         ; how fast (m/s) the right hand must move to count as a swing
swing = square            ; what a swing presses
swing_up = triangle       ; what an upward swing presses (Sly's uppercut)
layout = 4                ; (the button layout's version: an older file is moved on to the new defaults)
a = cross                 ; jump
b = circle                ; interact / pickpocket
x = triangle              ; uppercut
y = select                ; the game's gadget menu (used only with gadget_menu = button)
y_hold = start            ; pause
trigger_left = l1         ; use the gadget picked on the wristwatch
trigger_right = square    ; attack (with the cane drawn)
grip_left = none          ; (grabs: pockets, poles, ledges)
grip_right = r1           ; sprint while held / paraglider in the air (grabs where there is something to grab)
stick_click_left = sprint_toggle
stick_click_left_hold = l3       ; world stats
stick_click_right = r3           ; binocucom
stick_click_right_hold = none
menu = none
trackpad_right = none

[sly]

[bentley]
eye = 10                  ; he sits low in his wheelchair (a guess)
trigger_right = r1        ; darts on the trigger
grip_right = square       ; his dart-gun swing on the grip
swing_up = none           ; Triangle places a bomb: not on a gesture

[murray]
eye = 45                  ; he is tall (a guess)
swing_up = triangle       ; his uppercut
)";

using SECTION = std::map<std::string, std::string>;
std::string s_strPath;
std::map<std::string, SECTION> s_mpsec;
std::array<PROFILE, 4> s_aprofile;
float s_mPlayerEye = 0.0f;
bool s_fTPose = false;
glm::mat3 s_amatTPose[2]{glm::mat3(1.0f), glm::mat3(1.0f)};

std::string Trim(const std::string& str)
{
    size_t a = 0, b = str.size();
    while (a < b && std::isspace(static_cast<unsigned char>(str[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(str[b - 1]))) --b;
    return str.substr(a, b - a);
}

std::string Lower(std::string str)
{
    for (char& ch : str) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return str;
}

// A line's key and value ("key = value ; comment"); false: a section, a comment or blank.
bool FKeyValue(const std::string& line, std::string* pkey, std::string* pval)
{
    std::string l = line;
    if (const size_t i = l.find_first_of(";#"); i != std::string::npos) l = l.substr(0, i);
    const size_t eq = l.find('=');
    if (eq == std::string::npos) return false;
    *pkey = Lower(Trim(l.substr(0, eq)));
    *pval = Trim(l.substr(eq + 1));
    return !pkey->empty();
}

bool FSectionName(const std::string& line, std::string* pname)
{
    const std::string l = Trim(line);
    if (l.size() < 2 || l.front() != '[') return false;
    const size_t e = l.find(']');
    if (e == std::string::npos) return false;
    *pname = Lower(Trim(l.substr(1, e - 1)));
    return true;
}

bool FFloat(const std::string& str, float* pg)
{
    char* pchEnd = nullptr;
    const float g = std::strtof(str.c_str(), &pchEnd);
    if (pchEnd == str.c_str()) return false;
    *pg = g;
    return true;
}

bool FVec3(const std::string& str, glm::vec3* pv)
{
    glm::vec3 v(0.0f);
    if (std::sscanf(str.c_str(), "%f , %f , %f", &v.x, &v.y, &v.z) != 3) return false;
    *pv = v;
    return true;
}

bool FBool(const std::string& str, bool* pf)
{
    const std::string l = Lower(str);
    if (l == "1" || l == "true" || l == "on" || l == "yes") { *pf = true; return true; }
    if (l == "0" || l == "false" || l == "off" || l == "no") { *pf = false; return true; }
    return false;
}

PROFILE ProfileBuiltIn(Char c)
{
    PROFILE p;
    // (layout 3: the left grip grabs only; one gadget button, the left trigger; the gadget menu on the wristwatch, Y's
    // Select used only with gadget_menu = button)
    const uint32_t abtn[Ctl_Max] = {BTN_Cross, BTN_Circle, BTN_Triangle, BTN_Select, BTN_L1, BTN_Square, 0, BTN_R1,
                                    kBtnSprintToggle, BTN_R3, 0, 0};
    std::copy(std::begin(abtn), std::end(abtn), p.abtnCtl);
    p.abtnHold[Ctl_Y] = BTN_Start;              // (the pause: off the Menu button, Steam Link's own)
    p.abtnHold[Ctl_StickClickL] = BTN_L3;       // (world stats)
    p.fShowHands = true;
    p.btnSwing = BTN_Square;
    p.btnSwingUp = BTN_Triangle;
    if (c == Char::Bentley)
    {
        p.sEye = 10.0f;
        p.abtnCtl[Ctl_TriggerR] = BTN_R1;
        p.abtnCtl[Ctl_GripR] = BTN_Square;
        p.btnSwingUp = 0;
        p.handCharScale = 0.8f;
    }
    else if (c == Char::Murray)
    {
        p.sEye = 45.0f;
        p.handCharScale = 0.7f;
    }
    return p;
}

void Apply(const SECTION& sec, const char* szSection, PROFILE* pp)
{
    for (const auto& [key, val] : sec)
    {
        bool fOk = true;
        float g = 0.0f;
        if (key == "player_eye_height" || key == "mirror" || key == "layout" || key == "tpose_left" || key == "tpose_right" ||
            key == "tpose_span")
            continue;   // (global: MPlayerEye, StrSetting, FTPose)
        else if (key == "scale") fOk = FFloat(val, &g) && g > 0.0f && (pp->scale = g, true);
        else if (key == "eye") fOk = FFloat(val, &pp->sEye);
        else if (key == "swing_speed") fOk = FFloat(val, &g) && g > 0.1f && (pp->mpsSwing = g, true);
        // ("auto": the T-pose's turn; a character's own value wins over it)
        else if (key == "hand_rot" && Lower(val) == "auto") pp->fHandOwnR = false;
        else if (key == "hand_rot_left" && Lower(val) == "auto") pp->fHandL = pp->fHandOwnL = false;
        else if (key == "hand_rot") fOk = FVec3(val, &pp->degHandR) && (pp->fHandOwnR = std::string(szSection) != "default", true);
        else if (key == "hand_rot_left")
            fOk = FVec3(val, &pp->degHandL) && (pp->fHandL = true) && (pp->fHandOwnL = std::string(szSection) != "default", true);
        else if (key == "forearm") fOk = FBool(val, &pp->fForearm);
        else if (key == "calibrated") fOk = FBool(val, &pp->fCalibrated);
        else if (key == "show_hands") fOk = FBool(val, &pp->fShowHands);
        else if (key == "hand_size") fOk = FFloat(val, &g) && g >= 0.05f && g <= 50.0f && (pp->handSize = g, true);
        else if (key == "hand_scale") fOk = FFloat(val, &g) && g >= 0.05f && g <= 20.0f && (pp->handCharScale = g, true);
        else if (key == "turn")
        {
            const std::string l = Lower(val);
            fOk = l == "smooth" || l == "snap";
            if (fOk) pp->fSmoothTurn = l == "smooth";
        }
        else if (key == "turn_speed") fOk = FFloat(val, &g) && g >= 10.0f && g <= 720.0f && (pp->degsTurn = g, true);
        else if (key == "cane_fit")
        {
            const std::string l = Lower(val);
            fOk = l == "hand" || l == "grip";
            if (fOk) pp->fCaneOnHand = l == "hand";
        }
        else if (key == "hand_turn") fOk = FVec3(val, &pp->degHand);
        else if (key == "hand_offset") fOk = FVec3(val, &pp->cmHand);
        else if (key == "sly_size") fOk = FFloat(val, &g) && g >= 0.05f && g <= 50.0f && (pp->slySize = g, true);
        else if (key == "cane_size") fOk = FFloat(val, &g) && g >= 0.05f && g <= 50.0f && (pp->caneSize = g, true);
        else if (key == "cane_rot") fOk = FVec3(val, &pp->degCane);
        else if (key == "cane_offset") fOk = FVec3(val, &pp->cmCane);
        else if (key == "gadget_menu")
        {
            const std::string l = Lower(val);
            fOk = l == "watch" || l == "button";
            if (fOk) pp->nGadgetMenu = l == "watch" ? 0 : 1;
        }
        else if (key == "pickpocket")
        {
            const std::string l = Lower(val);
            fOk = l == "hold" || l == "grab" || l == "button";
            if (fOk) pp->nSteal = l == "hold" ? 0 : l == "grab" ? 1 : 2;
        }
        else if (key == "climb" || key == "glide")
        {
            const std::string l = Lower(val);
            const bool fPhys = key == "glide" ? l == "lines" : l == "grab";
            fOk = fPhys || l == (key == "glide" ? "stick" : "button");
            if (fOk) (key == "climb" ? pp->fGrabClimb : pp->fGlideLines) = fPhys;
        }
        else if (key == "hands")
        {
            int il = -1, ir = -1;
            if (Lower(val) == "auto") pp->iboneL = pp->iboneR = -1;
            else if (std::sscanf(val.c_str(), "%d , %d", &il, &ir) == 2) pp->iboneL = il, pp->iboneR = ir;
            else fOk = false;
        }
        else if (key == "swing" || key == "swing_up")
        {
            const uint32_t btn = BtnFromSz(val, &fOk);
            if (fOk && btn <= 0xFFFF) (key == "swing" ? pp->btnSwing : pp->btnSwingUp) = uint16_t(btn);
            else fOk = false;
        }
        else
        {
            int ctl = -1;
            bool fHold = false;
            for (int i = 0; i < Ctl_Max; ++i)
            {
                if (key == s_aszCtl[i]) ctl = i;
                else if (key == std::string(s_aszCtl[i]) + "_hold") ctl = i, fHold = true;
            }
            if (ctl < 0) fOk = false;
            else
            {
                const uint32_t btn = BtnFromSz(val, &fOk);
                if (fOk && fHold && btn == kBtnSprintToggle) fOk = false;   // (the toggle is a tap's only)
                if (fOk) (fHold ? pp->abtnHold : pp->abtnCtl)[ctl] = btn;   // (not understood: kept, as the message says)
            }
        }
        if (!fOk) std::fprintf(stderr, "vr: %s [%s] %s = %s: not understood (kept as before)\n", s_strPath.c_str(), szSection, key.c_str(), val.c_str());
    }
}

void ApplyEnv(PROFILE* pp)
{
    float g = 0.0f;
    if (const char* sz = std::getenv("SLY2N_VR_SCALE"); sz && FFloat(sz, &g) && g > 0.0f) pp->scale = g;
    if (const char* sz = std::getenv("SLY2N_VR_EYE"); sz && FFloat(sz, &g)) pp->sEye = g;
    if (const char* sz = std::getenv("SLY2N_VR_SWING"); sz && FFloat(sz, &g) && g > 0.1f) pp->mpsSwing = g;
    if (const char* sz = std::getenv("SLY2N_VR_HAND"); sz && FVec3(sz, &pp->degHandR)) pp->fHandOwnR = true;
    if (const char* sz = std::getenv("SLY2N_VR_FOREARM")) FBool(sz, &pp->fForearm);
    if (const char* sz = std::getenv("SLY2N_VR_HANDS"))
    {
        int il = -1, ir = -1;
        if (std::sscanf(sz, "%d , %d", &il, &ir) == 2) pp->iboneL = il, pp->iboneR = ir;
    }
}

void Resolve()
{
    s_mPlayerEye = 0.0f;
    if (auto it = s_mpsec.find("default"); it != s_mpsec.end())
        if (auto jt = it->second.find("player_eye_height"); jt != it->second.end()) FFloat(jt->second, &s_mPlayerEye);
    if (const char* sz = std::getenv("SLY2N_VR_PLAYER_EYE")) FFloat(sz, &s_mPlayerEye);
    if (!(s_mPlayerEye > 0.3f && s_mPlayerEye < 2.5f)) s_mPlayerEye = 0.0f;   // (nobody's eyes are lower or higher)
    // the T-pose: each grip's turn as a quaternion w,x,y,z (both, or neither)
    s_fTPose = false;
    if (auto it = s_mpsec.find("default"); it != s_mpsec.end())
    {
        int c = 0;
        for (int k = 0; k < 2; ++k)
            if (auto jt = it->second.find(k ? "tpose_right" : "tpose_left"); jt != it->second.end())
            {
                glm::quat q(1.0f, 0.0f, 0.0f, 0.0f);
                if (std::sscanf(jt->second.c_str(), "%f , %f , %f , %f", &q.w, &q.x, &q.y, &q.z) == 4 && glm::length(q) > 0.5f)
                {
                    s_amatTPose[k] = glm::mat3_cast(glm::normalize(q));
                    ++c;
                }
            }
        s_fTPose = c == 2;
    }
    if (const char* sz = std::getenv("SLY2N_VR_TPOSE"); sz && std::atof(sz) == 0.0) s_fTPose = false;   // (=0: ignore it)
    for (int c = 0; c < 4; ++c)
    {
        PROFILE p = ProfileBuiltIn(Char(c));
        if (auto it = s_mpsec.find("default"); it != s_mpsec.end()) Apply(it->second, "default", &p);
        if (c != 0)
            if (auto it = s_mpsec.find(s_aszChar[c]); it != s_mpsec.end()) Apply(it->second, s_aszChar[c], &p);
        ApplyEnv(&p);
        s_aprofile[size_t(c)] = p;
    }
}

std::vector<std::string> AstrReadLines(bool* pf)
{
    std::vector<std::string> astr;
    std::ifstream in(cane::path(s_strPath));
    *pf = bool(in);
    for (std::string line; std::getline(in, line);)
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        astr.push_back(line);
    }
    return astr;
}

bool FWriteLines(const std::vector<std::string>& astr)
{
    const std::string strTmp = s_strPath + ".tmp";
    {
        std::ofstream out(cane::path(strTmp), std::ios::binary | std::ios::trunc);
        if (!out) return false;
        for (const std::string& line : astr) out << line << "\r\n";   // (CRLF: Notepad on Windows)
        if (!out) return false;
    }
    std::error_code ec;
    cane::fs::rename(cane::path(strTmp), cane::path(s_strPath), ec);
    return !ec;
}
}  // namespace

const char* SzChar(Char c) { return s_aszChar[int(c)]; }
const char* SzCtl(int ctl) { return ctl >= 0 && ctl < Ctl_Max ? s_aszCtl[ctl] : "?"; }
const std::string& StrPath() { return s_strPath; }
const PROFILE& Profile(Char c) { return s_aprofile[size_t(c)]; }
float MPlayerEye() { return s_mPlayerEye; }
PROFILE ProfileBuiltInOf(Char c) { return ProfileBuiltIn(c); }
bool FHasKey(const char* szSection, const char* szKey)
{
    auto it = s_mpsec.find(Lower(szSection));
    return it != s_mpsec.end() && it->second.count(Lower(szKey)) != 0;
}
bool FTPose(glm::mat3* pmatL, glm::mat3* pmatR)
{
    if (!s_fTPose) return false;
    if (pmatL) *pmatL = s_amatTPose[0];
    if (pmatR) *pmatR = s_amatTPose[1];
    return true;
}

std::string StrSetting(const char* szKey, const char* szDefault)
{
    if (auto it = s_mpsec.find("default"); it != s_mpsec.end())
        if (auto jt = it->second.find(Lower(szKey)); jt != it->second.end()) return Lower(jt->second);
    return szDefault;
}

const char* SzButton(uint32_t btn)
{
    for (const BTNNAME& b : s_abtnname)
        if (b.btn == btn) return b.sz;
    return "none";
}

uint32_t BtnFromSz(const std::string& str, bool* pf)
{
    const std::string l = Lower(Trim(str));
    for (const BTNNAME& b : s_abtnname)
        if (l == b.sz) { if (pf) *pf = true; return b.btn; }
    if (pf) *pf = false;
    return 0;
}

bool FLoad()
{
    if (s_strPath.empty())
    {
        const char* sz = std::getenv("SLY2N_VR_INI");
        s_strPath = sz && *sz ? std::string(sz) : cane::utf8(cane::userDirectory() / "Sly2VR.ini");
    }
    bool fRead = false;
    std::vector<std::string> astr = AstrReadLines(&fRead);
    if (!fRead)
    {
        // the first time: the defaults written out with their comments, for editing
        std::istringstream in(s_szDefaultFile);
        astr.clear();
        for (std::string line; std::getline(in, line);) astr.push_back(line);
        if (FWriteLines(astr)) std::printf("vr: wrote the default settings to %s\n", s_strPath.c_str());
        else std::fprintf(stderr, "vr: could not write %s (the defaults are used)\n", s_strPath.c_str());
    }
    s_mpsec.clear();
    std::string strSec = "default";
    for (const std::string& line : astr)
    {
        std::string name, key, val;
        if (FSectionName(line, &name)) strSec = name;
        else if (FKeyValue(line, &key, &val)) s_mpsec[strSec][key] = val;
    }
    // (an older file: its layout keys still at their old defaults moved to the new ones, the missing ones added; keys
    // the player changed are kept)
    int layout = 1;
    if (auto it = s_mpsec.find("default"); it != s_mpsec.end())
        if (auto jt = it->second.find("layout"); jt != it->second.end()) layout = std::atoi(jt->second.c_str());
    if (layout < kLayout && fRead)
    {
        SECTION& def = s_mpsec["default"];
        for (const OLDKEY& k : s_aoldkey)
            if (layout < k.layout)
                if (auto jt = def.find(k.key); jt != def.end() && Lower(jt->second) == k.szOld) FSave("default", k.key, k.szNew);
        for (const OLDKEY& k : s_anewkey)
            if (layout < k.layout && !def.count(k.key)) FSave("default", k.key, k.szNew);
        FSave("default", "layout", std::to_string(kLayout));
        std::printf("vr: Sly2VR.ini moved from the button layout %d to %d (0.1.2: the hands shown, one gadget button, the grips "
                    "grab)\n", layout, kLayout);
    }
    Resolve();
    std::printf("vr: settings from %s\n", s_strPath.c_str());
    return true;
}

bool FSave(const char* szSection, const char* szKey, const std::string& strValue)
{
    bool fRead = false;
    std::vector<std::string> astr = AstrReadLines(&fRead);
    const std::string strSec = Lower(szSection), strKey = Lower(szKey);
    // the key's line in its section replaced (its comment kept), else added at the section's end, else a new section
    std::string strCur = "default";
    int iSecEnd = -1, iKey = -1;
    bool fSec = strSec == "default";
    for (size_t i = 0; i < astr.size(); ++i)
    {
        std::string name, key, val;
        if (FSectionName(astr[i], &name))
        {
            // (a header for the section already in force (the lines before [default] are [default]'s) goes on with it)
            if (name != strCur && strCur == strSec && iSecEnd < 0) iSecEnd = int(i);
            if (name == strSec && name != strCur) iSecEnd = -1;   // (the section again: its end is further on)
            strCur = name;
            if (name == strSec) fSec = true;
        }
        else if (strCur == strSec && FKeyValue(astr[i], &key, &val) && key == strKey) iKey = int(i);
    }
    if (strCur == strSec && iSecEnd < 0) iSecEnd = int(astr.size());
    const std::string strLine = strKey + " = " + strValue;
    if (iKey >= 0)
    {
        const size_t iComment = astr[size_t(iKey)].find_first_of(";#");
        astr[size_t(iKey)] = iComment == std::string::npos ? strLine : strLine + "   " + astr[size_t(iKey)].substr(iComment);
    }
    else if (fSec && iSecEnd >= 0)
    {
        // (before the section's trailing blank lines)
        int i = iSecEnd;
        while (i > 0 && Trim(astr[size_t(i - 1)]).empty()) --i;
        astr.insert(astr.begin() + i, strLine);
    }
    else
    {
        if (!astr.empty() && !Trim(astr.back()).empty()) astr.push_back("");
        astr.push_back("[" + strSec + "]");
        astr.push_back(strLine);
    }
    if (!FWriteLines(astr))
    {
        std::fprintf(stderr, "vr: could not write %s\n", s_strPath.c_str());
        return false;
    }
    s_mpsec[strSec][strKey] = strValue;
    Resolve();
    std::printf("vr: saved [%s] %s in %s\n", strSec.c_str(), strLine.c_str(), s_strPath.c_str());
    return true;
}
}  // namespace vrprofile
