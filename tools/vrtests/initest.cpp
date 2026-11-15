// (Sly 2 VR) Sly2VR.ini parser test: initest <a scratch .ini path> (tools/vrtests/README.md)
#include "vrprofile.h"
#include "joy.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <sstream>
#ifdef _WIN32   // (Windows has no setenv: the CRT's _putenv_s)
static int setenv(const char* k, const char* v, int) { return _putenv_s(k, v); }
static int unsetenv(const char* k) { return _putenv_s(k, ""); }
#endif
using namespace vrprofile;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
int main(int, char** argv) {
    std::remove(argv[1]);
    setenv("SLY2N_VR_INI", argv[1], 1);
    FLoad();                                   // writes the defaults
    const PROFILE& sly = Profile(Char::Sly);
    CHECK(sly.abtnCtl[Ctl_A] == BTN_Cross && sly.abtnCtl[Ctl_TriggerR] == BTN_Square && sly.abtnCtl[Ctl_GripR] == BTN_R1);
    CHECK(sly.btnSwing == BTN_Square && sly.btnSwingUp == BTN_Triangle && sly.sEye == 25.0f && sly.iboneR == -1);
    const PROFILE& ben = Profile(Char::Bentley);
    CHECK(ben.abtnCtl[Ctl_TriggerR] == BTN_R1 && ben.abtnCtl[Ctl_GripR] == BTN_Square && ben.btnSwingUp == 0 && ben.sEye == 10.0f);
    CHECK(Profile(Char::Murray).sEye == 45.0f && MPlayerEye() == 0.0f);
    // saves: an existing key (comment kept), a new key in an existing section, a new section
    CHECK(FSave("default", "player_eye_height", "1.62"));
    CHECK(FSave("murray", "hands", "12,20"));
    CHECK(FSave("sly", "hand_rot", "10.0,-5.5,90.0"));
    CHECK(FSave("carmelita", "eye", "30"));
    FLoad();
    CHECK(MPlayerEye() > 1.61f && MPlayerEye() < 1.63f);
    CHECK(Profile(Char::Murray).iboneL == 12 && Profile(Char::Murray).iboneR == 20);
    CHECK(Profile(Char::Sly).degHandR.z == 90.0f && Profile(Char::Bentley).degHandR.z == 0.0f);
    std::ifstream in(argv[1]); std::stringstream ss; ss << in.rdbuf(); const std::string t = ss.str();
    CHECK(t.find("player_eye_height = 1.62   ; your eye height") != std::string::npos);
    CHECK(t.find("[murray]") < t.find("hands = 12,20") && t.find("[carmelita]\r\neye = 30") != std::string::npos);
    // env override wins
    setenv("SLY2N_VR_EYE", "33", 1); FLoad(); CHECK(Profile(Char::Bentley).sEye == 33.0f);
    bool f = false; CHECK(BtnFromSz(" Triangle ", &f) == BTN_Triangle && f); BtnFromSz("bogus", &f); CHECK(!f);
    // the T-pose: both quaternions or none; a character's own hand_rot is "own", [default]'s and "auto" are not
    unsetenv("SLY2N_VR_EYE");
    CHECK(!FTPose(nullptr, nullptr));
    CHECK(Profile(Char::Sly).fHandOwnR && !Profile(Char::Bentley).fHandOwnR);
    CHECK(FSave("default", "tpose_left", "0.70711,0,0.70711,0"));
    FLoad(); CHECK(!FTPose(nullptr, nullptr));   // (one alone: none)
    CHECK(FSave("default", "tpose_right", "1,0,0,0"));
    FLoad();
    glm::mat3 mL(0.0f), mR(0.0f);
    CHECK(FTPose(&mL, &mR));
    CHECK(std::fabs(mR[0][0] - 1.0f) < 1e-4f && std::fabs(mL[0][2] + 1.0f) < 1e-3f);   // (90 degrees about Y: x -> -z)
    CHECK(FSave("sly", "hand_rot", "auto"));
    FLoad(); CHECK(!Profile(Char::Sly).fHandOwnR && Profile(Char::Sly).degHandR.z == 0.0f);
    CHECK(FSave("default", "hand_rot", "5,5,5"));
    FLoad(); CHECK(!Profile(Char::Murray).fHandOwnR && Profile(Char::Murray).degHandR.x == 5.0f);
    setenv("SLY2N_VR_TPOSE", "0", 1); FLoad(); CHECK(!FTPose(nullptr, nullptr)); unsetenv("SLY2N_VR_TPOSE");
    // (0.1.11) the binocucom: on the thigh, its lens auto at 40 degrees by default; the keys read; a bad value ignored
    CHECK(Profile(Char::Sly).nBinoc == 0 && Profile(Char::Sly).nBinocView == 0 && Profile(Char::Sly).degLens == 50.0f);
    CHECK(FSave("default", "binocucom", "stick") && FSave("bentley", "binoc_view", "dark") && FSave("default", "binoc_lens", "55"));
    FLoad();
    CHECK(Profile(Char::Sly).nBinoc == 1 && Profile(Char::Bentley).nBinocView == 2 && Profile(Char::Sly).nBinocView == 0);
    CHECK(Profile(Char::Murray).degLens == 55.0f);
    CHECK(FSave("default", "binoc_lens", "500")); FLoad(); CHECK(Profile(Char::Sly).degLens == 50.0f);
    std::printf("%s (%d failures)\n", fails ? "FAILED" : "all passed", fails);
    return fails != 0;
}
