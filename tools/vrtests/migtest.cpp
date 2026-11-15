// (Sly 2 VR) Sly2VR.ini layout migration test: migtest <a copy of v010.ini, v011.ini or v012.ini> 010|011|012 (README.md)
#include "vrprofile.h"
#include "joy.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#ifdef _WIN32   // (Windows has no setenv: the CRT's _putenv_s)
static int setenv(const char* k, const char* v, int) { return _putenv_s(k, v); }
static int unsetenv(const char* k) { return _putenv_s(k, ""); }
#endif
using namespace vrprofile;
// t <file> 010|011|012: an older Sly2VR.ini moved on to layout 4
int main(int, char** argv) {
    setenv("SLY2N_VR_INI", argv[1], 1);
    const bool f010 = std::string(argv[2]) == "010";
    FLoad();
    const PROFILE& p = Profile(Char::Sly);
    int f = 0;
    auto check = [&](bool c, const char* what) { if (!c) { std::printf("FAIL %s\n", what); ++f; } };
    check(p.abtnCtl[Ctl_A] == BTN_Circle, "a kept as the player set it");
    check(p.abtnCtl[Ctl_StickClickL] == kBtnSprintToggle, "stick_click_left = sprint_toggle");
    check(p.abtnCtl[Ctl_Menu] == 0, "menu = none");
    check(p.abtnHold[Ctl_Y] == BTN_Start, "y_hold = start");
    check(p.abtnHold[Ctl_StickClickL] == BTN_L3, "stick_click_left_hold = l3");
    check(p.abtnHold[Ctl_StickClickR] == 0, "stick_click_right_hold = none (layout 3)");
    check(p.abtnCtl[Ctl_GripL] == 0, "grip_left = none (layout 3)");
    check(p.abtnCtl[Ctl_PadR] == 0, "trackpad_right = none (layout 3)");
    check(p.fShowHands, "show_hands = 1 (layout 3)");
    check(p.slySize > 1.09f && p.caneSize < 0.91f && p.nGadgetMenu == 0 && p.fGrabClimb && p.fGlideLines, "0.1.2 defaults");
    check(p.nSteal == 0, "pickpocket = hold (layout 4)");
    check(Profile(Char::Bentley).abtnCtl[Ctl_TriggerR] == BTN_R1, "bentley darts on trigger");
    if (!f010) check(FTPose(nullptr, nullptr) && MPlayerEye() > 1.6f, "the T-pose and eye height kept");
    std::ifstream in(argv[1]); std::stringstream ss; ss << in.rdbuf();
    const std::string t = ss.str();
    check(t.find("a = circle                ; changed by the player") != std::string::npos, "comment kept");
    check(t.find("layout = 4") != std::string::npos && t.find("layout = 3") == std::string::npos && t.find("layout = 2") == std::string::npos,
          "layout 4 marked once");
    if (std::string(argv[2]) == "012") check(t.find("pickpocket = hold") != std::string::npos, "0.1.2's pickpocket = grab moved to hold");
    check(t.find("[default]") < t.find("grip_left = none") && t.find("grip_left = none") < t.find("[bentley]"), "keys in [default]");
    FLoad();   // (again: no second migration)
    check(Profile(Char::Sly).fShowHands && Profile(Char::Sly).abtnCtl[Ctl_GripL] == 0, "stable after a reload");
    std::printf("%s %s (%d failures)\n", argv[2], f ? "FAILED" : "all passed", f);
    return f != 0;
}
