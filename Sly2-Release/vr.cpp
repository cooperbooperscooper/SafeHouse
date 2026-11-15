// (PC, prototype) VR through OpenXR (vr.h). Compiled only with -DSLY2_VR=ON.
//
// The frame: BeginFrame waits for the headset (xrWaitFrame paces the loop in place of the frame-rate cap) and locates
// the eyes. Stereo: each eye's view is the game's camera (its yaw only, by default) times the head's pose scaled to
// the world's units; the scene is drawn into the usual scene target for each eye and blitted into that eye's
// swapchain image (the clip space fills the target either way, so the stretch keeps the eye's projection). The HUD is
// then drawn alone over a transparent target and goes to a quad layer in front of you. Screen mode: the normal
// picture goes to that quad, nothing else changes.
#if SLY2N_VR

#include "vr.h"
#include "gl.h"       // (glad first: GL/glx.h below then skips GL/gl.h)
#include "render.h"   // (gl.h brings GLFW and platform/gl_context.h: cane::gl::SwapInterval)
#include "joy.h"      // (BTN_*: the controllers' PS2 buttons)
#include "display.h"  // (g_display: the picture's rectangle in the window, for the mirror)
#include "vrprofile.h"
#include "rumble.h"   // (g_fControllerVibration: the launcher's Controller vibration, for Pulse)
#include "vrui.h"     // (IWatchEntryAt: the wristwatch's entries where DrawVrWatch draws them)

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <unknwn.h>   // (openxr_platform.h's Win32 part names IUnknown, which WIN32_LEAN_AND_MEAN leaves out)
#define XR_USE_PLATFORM_WIN32
#else
// (last: Xlib's macros (None, Bool, Status, ...) would break the game's headers)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/glx.h>
#define XR_USE_PLATFORM_XLIB
#endif
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#ifndef XR_API_VERSION_1_0   // (added in OpenXR-SDK 1.1.37; older 1.0.x / 1.1.36 headers, e.g. Linux distributions', lack it)
#define XR_API_VERSION_1_0 XR_MAKE_VERSION(1, 0, XR_VERSION_PATCH(XR_CURRENT_API_VERSION))
#endif

extern bool g_fHudWritesAlpha;   // (hud.cpp: the HUD writes its coverage into alpha, for the VR HUD layer)

namespace vr
{
namespace
{
struct SWAP
{
    XrSwapchain h = XR_NULL_HANDLE;
    int dx = 0, dy = 0;
    std::vector<XrSwapchainImageOpenGLKHR> aimg;
};

struct STATE
{
    Mode mode = Mode::Off;
    XrInstance inst = XR_NULL_HANDLE;
    XrSystemId sys = XR_NULL_SYSTEM_ID;
    XrSession sess = XR_NULL_HANDLE;
    XrSpace space = XR_NULL_HANDLE;
    bool fRunning = false, fLost = false;
    int64_t format = 0;
    SWAP aeye[2], panel;
    XrView aview[2]{};
    XrCompositionLayerProjectionView aprojv[2]{};
    XrFrameState fs{};
    bool fBegun = false, fShould = false, fViews = false, afEye[2]{}, fPanel = false;
    bool fPanelScreen = false;    // (this frame's panel is the big opaque screen: a movie, a cutscene)
    float dScreen = 3.0f;         // the big screen's distance (m)
    GLuint fboBlit = 0;
    // tuning (vr.h; the per-character settings are vrprofile.h's)
    float scale = 100.0f;         // game units per metre this frame (the profile's, or from the height calibration)
    bool fLevel = true;
    float dPanel = 3.0f;
    vrprofile::Char charCur = vrprofile::Char::Sly;
    float sEyeAboveFloor = 0.0f;  // the playing character's eye above its floor, game units (0: unknown)
    // the floor-level space (the height calibration): STAGE, and the head's VIEW space located in it
    XrSpace spaceStage = XR_NULL_HANDLE, spaceView = XR_NULL_HANDLE;
    // the recentred origin (LOCAL space): where the head was and the yaw it faced
    glm::vec3 posOrigin{0.0f};
    float yawOrigin = 0.0f;
    bool fRecenter = true, fF8Was = false;
    // the controllers (actions): the physical controls (vrprofile.h Ctl_*), the two thumbsticks, the grip and aim poses
    XrActionSet actionset = XR_NULL_HANDLE;
    XrAction aactCtl[vrprofile::Ctl_Max]{};
    bool afCtlPrev[vrprofile::Ctl_Max]{};
    bool afCtlChord[vrprofile::Ctl_Max]{};   // (a button that started a helper: kept from the game until released)
    // controls with a long press (vrprofile.h _hold keys): when pressed, whether the hold began, a tap's steps left
    double atDown[vrprofile::Ctl_Max]{};
    bool afHoldFired[vrprofile::Ctl_Max]{};
    int acstepTap[vrprofile::Ctl_Max]{};
    // the sprint toggle (vrprofile.h kBtnSprintToggle): R1 kept on; when the left stick came to rest (-1: moving)
    bool fSprint = false;
    double tSprintRest = -1.0;
    uint16_t grfVrPrev = 0;
    bool fIndex = false;          // the Valve Index's profile is the one in use (the button prompts)
    bool fPromptVr = false;       // the VR controllers were used last (the button prompts)
    // the left stick click's chord (vr.h): held with A / B / X / Y for the helpers; alone, tapped through to the game
    bool fChord = false, fChordUsed = false;
    double tChordDown = 0.0;
    // the calibrations: the hands frozen at the animation's pose (vrbody.h offers it each frame)
    bool fHandCal = false;
    // the T-pose calibration (vr.h NoteTitle): 0 off, 1 posing, 2 done (its message a moment)
    int tposeStep = 0;
    bool fTPoseAsked = false;     // (asked for this session already: at the title, or from the menu)
    bool fCalForced = false;      // (SLY2N_VR_CALIBRATE=1: asked for whatever Sly2VR.ini says)
    bool fCanCalNow = false;      // (SetCharacter: first person in play, no cutscene or menu)
    double tTPoseHold = -1.0, tTPoseDone = 0.0;   // (when the still hold began, -1: not holding; when it was saved)
    double tTPoseGrip = 0.0;      // (the right controller, B's, last tracked while asked for)
    bool fTPoseWanted = false;    // (asked for from the menu: again once the controllers are back, though one is saved)
    glm::vec3 aposTPose[2]{};     // (where the hands were when the hold began)
    const char* szTPoseHint = nullptr;
    // the playing character's hands in its own T-pose, in its body's axes (vr.h SetCharacterTPose)
    bool fCharT = false;
    glm::mat3 amatCharT[2]{glm::mat3(1.0f), glm::mat3(1.0f)};
    glm::mat4 amatHandAnim[2]{};
    bool afHandAnim[2]{};
    // auto-pause: the session lost its focus (the headset off, the system menu): Start pressed once
    bool fFocused = false, fPauseWanted = false;
    int cstepPause = 0;
    // the right controller in use lately (FAimRay): when it last moved or a right-hand control was used
    double tRightActive = -1e9;
    XrPosef poseAimRef{};
    bool fAimRef = false;
    XrAction actMove = XR_NULL_HANDLE, actTurn = XR_NULL_HANDLE, actGrip = XR_NULL_HANDLE, actAim = XR_NULL_HANDLE;
    XrPath apathHand[2]{};
    XrSpace aspaceGrip[2]{}, aspaceAim[2]{};
    XrPosef aposeGrip[2]{}, aposeAim[2]{};
    bool afGrip[2]{}, afAim[2]{};
    // the aim: the camera the eyes were last drawn from (MatCamOf), the pointer's ends and how long it stays
    glm::mat4 matCamLast{1.0f};
    bool fCamLast = false;
    glm::vec3 posPointerFrom{0.0f}, posPointerTo{0.0f};
    int cframePointer = 0;
    // (the watch's laser: kept in LOCAL space, put in the world as each frame is drawn: it stays on the hand)
    bool fPointerLocal = false;
    glm::vec3 posPtrFromLocal{0.0f}, posPtrToLocal{0.0f};
    GLuint progPointer = 0, vaoPointer = 0, vboPointer = 0;
    GLint iuColor = -1;
    // the desktop window's mirror (FPresentMirror): the left eye's picture kept, and the HUD drawn over it
    GLuint fboMirror = 0, texMirror = 0, progQuad = 0;
    int dxMirror = 0, dyMirror = 0;
    bool fMirrorFrame = false;
    XrFovf fovMirror{};
    bool fActions = false;
    // first person
    bool fFpEyes = false;         // (game.cpp SetFirstPersonEyes: the eyes at the player's head this frame)
    double tBlink = -1e9;         // (BlinkView: when the last cut was)
    bool fBodyInit = false;
    float yawBody = 0.0f;         // the body's world heading (radians, atan2(y, x))
    bool fSnapWas = false;
    int cstepSwingCool = 0, cstepSwingHold = 0;
    uint16_t btnSwingHeld = 0;
    // (0.1.3) a swing is decided a few steps after it starts, on its strongest upward push: an uppercut begins as
    // a sweep and only then goes up, so the first step's direction missed it
    int cstepSwingDecide = 0;
    float vyPeak = 0.0f, speedPeak = 0.0f;
    // the head as last drawn (FHeadView): the eyes' world matrices this frame, then the combined view
    glm::mat4 amatEyeWorld[2]{};
    bool afEyeWorld[2]{};
    bool fHead = false;
    glm::vec3 posHead{0.0f};
    glm::mat3 matHeadRows{1.0f};
    float tanHeadX = 1.0f, tanHeadY = 1.0f;
    uint64_t serialHead = 0;
    // (0.1.2) the physical interactions (vr.h): the vibration, the cane's holster, the grips, vrinteract's requests
    XrAction actHaptic = XR_NULL_HANDLE;   // (vibration output, both hands: Pulse)
    bool fCaneDrawn = true;
    bool fAtHolster = false;      // (the right hand behind the right shoulder, as of the last read)
    bool fTorso = false;          // (no torso tracking: the body's yaw in LOCAL, lagging the head's)
    float yawTorso = 0.0f;
    double tTorso = 0.0;
    bool afGripHeld[2]{};
    bool afGripPending[2]{};      // (a new press for FTakeGripPress, for a few steps)
    int acstepGripPending[2]{};
    bool afGripGrab[2]{};         // (this hold is a grab: its button kept from the game until let go)
    bool afGrabZone[2]{}, fPocketNear = false;   // (vrinteract's, set the step before)
    glm::vec3 avelGripLocal[2]{};  // (each grip's velocity as of the last read: LOCAL, m/s)
    bool afVelGrip[2]{};
    int acstepReq[16]{};          // (RequestButtons: the steps left for each PS2 bit)
    bool fClimbStick = false;     // (SetClimbStick: the left stick given to the game)
    glm::vec2 vClimbStick{0.0f};
    int cstepClimbStick = 0;      // (reads since it was last set: a stale one lets go)
    // the wristwatch (gadget_menu = watch): open (debounced), its entries, the left hand's laser on it, its picture
    bool fWatchOpen = false;
    double tWatchSteady = 0.0;    // (when the wrist last agreed with the open state)
    std::vector<std::string> astrWatch;
    int iWatchCur = -1, iWatchHover = -1, iWatchPick = -1;
    SWAP watch;
    bool fWatchFrame = false;     // (SubmitWatch ran this frame: EndFrame shows its quad)
    // (BeginWatchLayer after SubmitPanel: the HUD the panel showed, kept for the window's mirror)
    GLuint fboMirrorHud = 0, texMirrorHud = 0;
    int dxMirrorHud = 0, dyMirrorHud = 0;
    bool fMirrorHudKept = false;
};
STATE s;

double TNow()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
const vrprofile::PROFILE& Prof() { return vrprofile::Profile(s.charCur); }

bool FCheck(XrResult r, const char* what)
{
    if (XR_SUCCEEDED(r)) return true;
    char ach[XR_MAX_RESULT_STRING_SIZE] = "?";
    if (s.inst) xrResultToString(s.inst, r, ach);
    std::fprintf(stderr, "vr: %s failed: %s (%d)\n", what, ach, int(r));
    return false;
}

glm::vec3 Vec(const XrVector3f& v) { return glm::vec3(v.x, v.y, v.z); }
glm::quat Quat(const XrQuaternionf& q) { return glm::quat(q.w, q.x, q.y, q.z); }

// GL state the blits change, put back after them (the game's draws that follow expect their own).
struct GLGUARD
{
    GLint fboDraw = 0, fboRead = 0;
    GLboolean fScissor = GL_FALSE, fSrgb = GL_FALSE, amask[4]{};
    GLGUARD()
    {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fboDraw);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &fboRead);
        fScissor = glIsEnabled(GL_SCISSOR_TEST);
        fSrgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
        glGetBooleanv(GL_COLOR_WRITEMASK, amask);
        glDisable(GL_SCISSOR_TEST);       // (a blit is scissored)
        glDisable(GL_FRAMEBUFFER_SRGB);   // (the bytes copied as they are: the picture is already gamma encoded)
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    ~GLGUARD()
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(fboDraw));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(fboRead));
        if (fScissor) glEnable(GL_SCISSOR_TEST);
        if (fSrgb) glEnable(GL_FRAMEBUFFER_SRGB);
        glColorMask(amask[0], amask[1], amask[2], amask[3]);
    }
};

bool FCreateSwap(int dx, int dy, SWAP* psw)
{
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    ci.format = s.format;
    ci.sampleCount = 1;
    ci.width = uint32_t(dx);
    ci.height = uint32_t(dy);
    ci.faceCount = 1;
    ci.arraySize = 1;
    ci.mipCount = 1;
    if (!FCheck(xrCreateSwapchain(s.sess, &ci, &psw->h), "xrCreateSwapchain")) return false;
    uint32_t c = 0;
    if (!FCheck(xrEnumerateSwapchainImages(psw->h, 0, &c, nullptr), "xrEnumerateSwapchainImages")) return false;
    psw->aimg.assign(c, XrSwapchainImageOpenGLKHR{XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
    if (!FCheck(xrEnumerateSwapchainImages(psw->h, c, &c, reinterpret_cast<XrSwapchainImageBaseHeader*>(psw->aimg.data())),
                "xrEnumerateSwapchainImages"))
        return false;
    psw->dx = dx;
    psw->dy = dy;
    return true;
}

void DestroySwap(SWAP* psw)
{
    if (psw->h) xrDestroySwapchain(psw->h);
    *psw = SWAP{};
}

// The scene target as it is now into the next image of the swapchain (MSAA resolved first, at the scene's size);
// sx0 .. sy1: only that rectangle of it (the wristwatch: the 4:3 canvas), -1: all of it.
void BlitSceneInto(SWAP& sw, int sx0 = 0, int sy0 = 0, int sx1 = -1, int sy1 = -1)
{
    if (sx1 < 0) sx0 = 0, sy0 = 0, sx1 = g_gl.renderWidth, sy1 = g_gl.renderHeight;
    GLGUARD guard;
    if (g_gl.fboMsaa)
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, g_gl.fboMsaa);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_gl.fbo);
        glBlitFramebuffer(0, 0, g_gl.renderWidth, g_gl.renderHeight, 0, 0, g_gl.renderWidth, g_gl.renderHeight,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    uint32_t i = 0;
    if (!FCheck(xrAcquireSwapchainImage(sw.h, &ai, &i), "xrAcquireSwapchainImage")) return;
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wi.timeout = XR_INFINITE_DURATION;
    if (FCheck(xrWaitSwapchainImage(sw.h, &wi), "xrWaitSwapchainImage"))
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, g_gl.fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s.fboBlit);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sw.aimg[i].image, 0);
        glBlitFramebuffer(sx0, sy0, sx1, sy1, 0, 0, sw.dx, sw.dy, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    }
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    FCheck(xrReleaseSwapchainImage(sw.h, &ri), "xrReleaseSwapchainImage");
}

void PollEvents()
{
    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(s.inst, &ev) == XR_SUCCESS)
    {
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
        {
            const auto& sc = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&ev);
            switch (sc.state)
            {
            case XR_SESSION_STATE_READY:
            {
                XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                s.fRunning = FCheck(xrBeginSession(s.sess, &bi), "xrBeginSession");
                break;
            }
            case XR_SESSION_STATE_FOCUSED:
                s.fFocused = true;
                break;
            case XR_SESSION_STATE_VISIBLE:
            case XR_SESSION_STATE_SYNCHRONIZED:
            case XR_SESSION_STATE_IDLE:
                // (the system menu over the game, or the headset taken off: GrfAutoPause pauses the game once)
                if (s.fFocused) s.fPauseWanted = true;
                s.fFocused = false;
                break;
            case XR_SESSION_STATE_STOPPING:
                if (s.fFocused) s.fPauseWanted = true;
                s.fFocused = false;
                xrEndSession(s.sess);
                s.fRunning = false;
                break;
            case XR_SESSION_STATE_EXITING:
            case XR_SESSION_STATE_LOSS_PENDING:
                s.fRunning = false;
                s.fLost = true;
                break;
            default:
                break;
            }
        }
        else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
            s.fLost = true;
        else if (ev.type == XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED && s.apathHand[1])
        {
            // (which controllers: the button prompts show the Index's icons for an Index, the Touch's otherwise)
            XrInteractionProfileState ips{XR_TYPE_INTERACTION_PROFILE_STATE};
            if (XR_SUCCEEDED(xrGetCurrentInteractionProfile(s.sess, s.apathHand[1], &ips)) && ips.interactionProfile)
            {
                char ach[XR_MAX_PATH_LENGTH] = "";
                uint32_t c = 0;
                xrPathToString(s.inst, ips.interactionProfile, sizeof ach, &c, ach);
                s.fIndex = std::strstr(ach, "index_controller") != nullptr;
                std::printf("vr: controllers: %s\n", ach);
            }
        }
        ev = XrEventDataBuffer{XR_TYPE_EVENT_DATA_BUFFER};
    }
}

float FloatEnv(const char* name, float gDefault)
{
    const char* sz = std::getenv(name);
    return sz && *sz ? float(std::atof(sz)) : gDefault;
}

XrPath PathOf(const char* sz)
{
    XrPath path = XR_NULL_PATH;
    xrStringToPath(s.inst, sz, &path);
    return path;
}

// The actions and each controller's suggested bindings (a runtime takes the profile of the controllers it has; the
// Quest 3's Touch Plus controllers answer to the Touch profile). Before the session's xrAttachSessionActionSets.
bool FCreateActions()
{
    XrActionSetCreateInfo asci{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::snprintf(asci.actionSetName, XR_MAX_ACTION_SET_NAME_SIZE, "gameplay");
    std::snprintf(asci.localizedActionSetName, XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE, "Gameplay");
    if (!FCheck(xrCreateActionSet(s.inst, &asci, &s.actionset), "xrCreateActionSet")) return false;
    s.apathHand[0] = PathOf("/user/hand/left");
    s.apathHand[1] = PathOf("/user/hand/right");
    auto create = [](const char* name, XrActionType type, bool fHands, XrAction* pact) {
        XrActionCreateInfo aci{XR_TYPE_ACTION_CREATE_INFO};
        aci.actionType = type;
        std::snprintf(aci.actionName, XR_MAX_ACTION_NAME_SIZE, "%s", name);
        std::snprintf(aci.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE, "%s", name);
        if (fHands)
        {
            aci.countSubactionPaths = 2;
            aci.subactionPaths = s.apathHand;
        }
        return FCheck(xrCreateAction(s.actionset, &aci, pact), "xrCreateAction");
    };
    for (int i = 0; i < vrprofile::Ctl_Max; ++i)
        if (!create(vrprofile::SzCtl(i), XR_ACTION_TYPE_BOOLEAN_INPUT, false, &s.aactCtl[i])) return false;
    if (!create("move", XR_ACTION_TYPE_VECTOR2F_INPUT, false, &s.actMove)) return false;
    if (!create("turn", XR_ACTION_TYPE_VECTOR2F_INPUT, false, &s.actTurn)) return false;
    if (!create("grip", XR_ACTION_TYPE_POSE_INPUT, true, &s.actGrip)) return false;
    if (!create("aim", XR_ACTION_TYPE_POSE_INPUT, true, &s.actAim)) return false;
    // (the vibration: without it the controls above still play, a failure only logs)
    if (!create("haptic", XR_ACTION_TYPE_VIBRATION_OUTPUT, true, &s.actHaptic)) s.actHaptic = XR_NULL_HANDLE;

    // iact: a control (vrprofile.h Ctl_*), or 100 move, 101 turn, 102 / 103 the grips, 104 / 105 the aims, 106 / 107
    // the vibrations
    struct BIND { int iact; const char* path; };
    using namespace vrprofile;
    auto suggest = [](const char* profile, std::initializer_list<BIND> abind) {
        std::vector<XrActionSuggestedBinding> asb;
        for (const BIND& b : abind)
        {
            const XrAction act = b.iact < Ctl_Max ? s.aactCtl[b.iact] : b.iact == 100 ? s.actMove : b.iact == 101 ? s.actTurn
                               : b.iact <= 103 ? s.actGrip : b.iact <= 105 ? s.actAim : s.actHaptic;
            if (act) asb.push_back({act, PathOf(b.path)});   // (an action not made: its binding left out, not the profile)
        }
        XrInteractionProfileSuggestedBinding sb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        sb.interactionProfile = PathOf(profile);
        sb.countSuggestedBindings = uint32_t(asb.size());
        sb.suggestedBindings = asb.data();
        // (one profile refused does not stop the others)
        FCheck(xrSuggestInteractionProfileBindings(s.inst, &sb), profile);
    };
    // Meta Quest Touch (Quest 2 / 3 / Pro, through Link, Air Link, Virtual Desktop or SteamVR): the physical controls;
    // what each presses is the character's profile (vrprofile.h, Sly2VR.ini)
    suggest("/interaction_profiles/oculus/touch_controller", {
        {Ctl_A, "/user/hand/right/input/a/click"}, {Ctl_B, "/user/hand/right/input/b/click"},
        {Ctl_X, "/user/hand/left/input/x/click"}, {Ctl_Y, "/user/hand/left/input/y/click"},
        {Ctl_TriggerL, "/user/hand/left/input/trigger/value"}, {Ctl_TriggerR, "/user/hand/right/input/trigger/value"},
        {Ctl_GripL, "/user/hand/left/input/squeeze/value"}, {Ctl_GripR, "/user/hand/right/input/squeeze/value"},
        {Ctl_StickClickL, "/user/hand/left/input/thumbstick/click"}, {Ctl_StickClickR, "/user/hand/right/input/thumbstick/click"},
        {Ctl_Menu, "/user/hand/left/input/menu/click"},
        {100, "/user/hand/left/input/thumbstick"}, {101, "/user/hand/right/input/thumbstick"},
        {102, "/user/hand/left/input/grip/pose"}, {103, "/user/hand/right/input/grip/pose"},
        {104, "/user/hand/left/input/aim/pose"}, {105, "/user/hand/right/input/aim/pose"},
        {106, "/user/hand/left/output/haptic"}, {107, "/user/hand/right/output/haptic"}});
    // Valve Index: the left hand's A / B as X / Y, the left trackpad squeezed as Menu, the right one as PadR.
    suggest("/interaction_profiles/valve/index_controller", {
        {Ctl_A, "/user/hand/right/input/a/click"}, {Ctl_B, "/user/hand/right/input/b/click"},
        {Ctl_X, "/user/hand/left/input/a/click"}, {Ctl_Y, "/user/hand/left/input/b/click"},
        {Ctl_TriggerL, "/user/hand/left/input/trigger/value"}, {Ctl_TriggerR, "/user/hand/right/input/trigger/value"},
        {Ctl_GripL, "/user/hand/left/input/squeeze/value"}, {Ctl_GripR, "/user/hand/right/input/squeeze/value"},
        {Ctl_StickClickL, "/user/hand/left/input/thumbstick/click"}, {Ctl_StickClickR, "/user/hand/right/input/thumbstick/click"},
        {Ctl_Menu, "/user/hand/left/input/trackpad/force"}, {Ctl_PadR, "/user/hand/right/input/trackpad/force"},
        {100, "/user/hand/left/input/thumbstick"}, {101, "/user/hand/right/input/thumbstick"},
        {102, "/user/hand/left/input/grip/pose"}, {103, "/user/hand/right/input/grip/pose"},
        {104, "/user/hand/left/input/aim/pose"}, {105, "/user/hand/right/input/aim/pose"},
        {106, "/user/hand/left/output/haptic"}, {107, "/user/hand/right/output/haptic"}});
    return true;
}

bool FAttachActions()
{
    XrSessionActionSetsAttachInfo ai{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    ai.countActionSets = 1;
    ai.actionSets = &s.actionset;
    if (!FCheck(xrAttachSessionActionSets(s.sess, &ai), "xrAttachSessionActionSets")) return false;
    for (int k = 0; k < 2; ++k)
    {
        XrActionSpaceCreateInfo sci{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        sci.action = s.actGrip;
        sci.subactionPath = s.apathHand[k];
        sci.poseInActionSpace.orientation.w = 1.0f;
        if (!FCheck(xrCreateActionSpace(s.sess, &sci, &s.aspaceGrip[k]), "xrCreateActionSpace")) return false;
        sci.action = s.actAim;
        if (!FCheck(xrCreateActionSpace(s.sess, &sci, &s.aspaceAim[k]), "xrCreateActionSpace")) return false;
    }
    return true;
}

// The game's camera in GL eye axes (columns: right, up, back, position) in the world's units (Z up); with the level
// option, its yaw only (the game's pitch and roll in a headset sicken; the head does the looking up and down).
glm::mat4 MatCamOf(const CAMERA& cam)
{
    glm::vec3 fwd = cam.fMat ? cam.mat[0] : cam.Dir();
    glm::vec3 up = cam.fMat ? cam.mat[2] : glm::vec3(0.0f, 0.0f, 1.0f);
    if (s.fLevel || s.mode == Mode::FirstPerson)
    {
        glm::vec3 h(fwd.x, fwd.y, 0.0f);
        if (glm::length(h) < 1e-4f) h = glm::vec3(up.x, up.y, 0.0f);   // (looking straight down: its up is forward)
        if (glm::length(h) < 1e-4f) h = glm::vec3(1.0f, 0.0f, 0.0f);
        fwd = glm::normalize(h);
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    const glm::vec3 right = glm::normalize(glm::cross(fwd, up));
    up = glm::cross(right, fwd);
    glm::mat4 matCam(1.0f);
    matCam[0] = glm::vec4(right, 0.0f);
    matCam[1] = glm::vec4(up, 0.0f);
    matCam[2] = glm::vec4(-fwd, 0.0f);
    matCam[3] = glm::vec4(cam.pos, 1.0f);
    return matCam;
}

// A tracked pose (OpenXR's LOCAL space: Y up, -Z forward, metres = GL eye axes) from the recentred origin, in the
// camera's space: the position scaled to the world's units, the turn kept.
glm::mat4 MatTrackedLocal(const XrPosef& pose)
{
    const glm::quat qUnyaw = glm::angleAxis(-s.yawOrigin, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 pos = qUnyaw * (Vec(pose.position) - s.posOrigin) * s.scale;
    return glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(qUnyaw * Quat(pose.orientation));
}

// The head's yaw from the recentred forward (radians, + to the left), from the last located eyes.
float RadHeadYaw()
{
    const glm::quat qUnyaw = glm::angleAxis(-s.yawOrigin, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 fwd = qUnyaw * Quat(s.aview[0].pose.orientation) * glm::vec3(0.0f, 0.0f, -1.0f);
    return std::atan2(-fwd.x, -fwd.z);
}

// The hand calibration's turn of hand k on its grip (vrprofile.h hand_rot / hand_rot_left; the left's default is the
// right's mirror image across the grip's X, where the hands mirror)
glm::mat4 MatHandCal(int k)
{
    const vrprofile::PROFILE& pr = Prof();
    // the T-pose's: your grip and the character's hand each in its own T-pose, in its body's axes (right, up, back):
    // grip x cal = hand, so cal = grip^T x hand (unless this character has its own turn set: a fine-tuning)
    glm::mat3 amatT[2];
    if (s.fCharT && !(k == 1 ? pr.fHandOwnR : pr.fHandOwnL) && vrprofile::FTPose(&amatT[0], &amatT[1]))
        return glm::mat4(glm::transpose(amatT[k]) * s.amatCharT[k]);
    const glm::mat4 matR = glm::mat4_cast(glm::quat(glm::radians(pr.degHandR)));
    if (k == 1) return matR;
    if (pr.fHandL) return glm::mat4_cast(glm::quat(glm::radians(pr.degHandL)));
    const glm::mat4 matMirror = glm::scale(glm::mat4(1.0f), glm::vec3(-1.0f, 1.0f, 1.0f));
    return matMirror * matR * matMirror;
}

glm::mat3 MatTurnOf(const glm::mat4& m)
{
    glm::mat3 r(m);
    for (int i = 0; i < 3; ++i) r[i] = glm::normalize(r[i]);
    return r;
}

// The mirror's mode: 0 the HUD only (no copy), 1 the left eye only, 2 the left eye with the HUD over it
int NMirror()
{
    const char* sz = std::getenv("SLY2N_VR_MIRROR");
    const std::string str = sz && *sz ? std::string(sz) : vrprofile::StrSetting("mirror", "left");
    return str == "hud" ? 0 : str == "eye" ? 1 : 2;
}

// A copy of the resolved scene target (g_gl.fbo) at its size, into *ptex through *pfbo (made, or made again at a new
// size); fSampled: drawn from afterwards (linear filtering)
void CopyResolved(GLuint* pfbo, GLuint* ptex, int* pdx, int* pdy, bool fSampled)
{
    if (!*pfbo || *pdx != g_gl.renderWidth || *pdy != g_gl.renderHeight)
    {
        if (!*pfbo) glGenFramebuffers(1, pfbo);
        if (*ptex) glDeleteTextures(1, ptex);
        glGenTextures(1, ptex);
        GLint tex = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
        glBindTexture(GL_TEXTURE_2D, *ptex);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, g_gl.renderWidth, g_gl.renderHeight);
        if (fSampled)
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        glBindTexture(GL_TEXTURE_2D, GLuint(tex));
        GLGUARD guard;
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, *pfbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *ptex, 0);
        *pdx = g_gl.renderWidth;
        *pdy = g_gl.renderHeight;
    }
    GLGUARD guard;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_gl.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, *pfbo);
    glBlitFramebuffer(0, 0, *pdx, *pdy, 0, 0, *pdx, *pdy, GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

// The left eye's resolved picture (g_gl.fbo after BlitSceneInto) copied for the desktop window
void KeepMirror()
{
    if (NMirror() == 0) return;
    CopyResolved(&s.fboMirror, &s.texMirror, &s.dxMirror, &s.dyMirror, false);
    s.fMirrorFrame = true;
    s.fovMirror = s.aview[0].fov;
}

// (BeginWatchLayer after SubmitPanel) the HUD the panel showed (resolved into g_gl.fbo by its blit) kept for the
// mirror's HUD (mirror = left or hud), as the scene target is about to show the watch
void KeepMirrorHud()
{
    if (NMirror() == 1) return;
    CopyResolved(&s.fboMirrorHud, &s.texMirrorHud, &s.dxMirrorHud, &s.dyMirrorHud, true);
    s.fMirrorHudKept = true;
}

// A textured quad over the viewport (the HUD over the mirror; blended as premultiplied by FPresentMirror)
bool FQuadProgram()
{
    if (s.progQuad) return true;
    const char* szVs = "#version 430 core\nout vec2 vUv;\nvoid main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);"
                       " vUv = p; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n";
    const char* szFs = "#version 430 core\nin vec2 vUv;\nuniform sampler2D uTex;\nout vec4 oColor;\n"
                       "void main() { oColor = texture(uTex, vUv); }\n";
    auto compile = [](GLenum type, const char* sz) {
        const GLuint sh = glCreateShader(type);
        glShaderSource(sh, 1, &sz, nullptr);
        glCompileShader(sh);
        GLint f = 0;
        glGetShaderiv(sh, GL_COMPILE_STATUS, &f);
        if (!f) { glDeleteShader(sh); return GLuint(0); }
        return sh;
    };
    const GLuint vs = compile(GL_VERTEX_SHADER, szVs), fs = compile(GL_FRAGMENT_SHADER, szFs);
    if (vs && fs)
    {
        s.progQuad = glCreateProgram();
        glAttachShader(s.progQuad, vs);
        glAttachShader(s.progQuad, fs);
        glLinkProgram(s.progQuad);
        GLint f = 0;
        glGetProgramiv(s.progQuad, GL_LINK_STATUS, &f);
        if (!f) glDeleteProgram(s.progQuad), s.progQuad = 0;
    }
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    if (!s.progQuad) std::fprintf(stderr, "vr: the mirror's HUD program did not build (the eye only)\n");
    return s.progQuad != 0;
}

// The hand calibration's capture: each hand shown frozen and tracked gets its turn saved (hand_rot / hand_rot_left of
// the playing character); the count captured.
int CaptureHands()
{
    int c = 0;
    for (int k = 0; k < 2; ++k)
    {
        if (!s.afHandAnim[k] || !s.afGrip[k] || !s.fCamLast) continue;
        // the grip's turn in the world (without the old calibration) onto the hand's: grip x cal = hand
        const glm::mat3 matGrip = MatTurnOf(s.matCamLast * MatTrackedLocal(s.aposeGrip[k]));
        const glm::mat3 matCal = glm::transpose(matGrip) * MatTurnOf(s.amatHandAnim[k]);
        const glm::vec3 deg = glm::degrees(glm::eulerAngles(glm::quat_cast(matCal)));
        char ach[64];
        std::snprintf(ach, sizeof ach, "%.1f,%.1f,%.1f", deg.x, deg.y, deg.z);
        vrprofile::FSave(vrprofile::SzChar(s.charCur), k == 1 ? "hand_rot" : "hand_rot_left", ach);
        ++c;
    }
    if (!c) std::fprintf(stderr, "vr: hand calibration: no hand was shown and tracked (first person, the hands found?)\n");
    return c;
}

// Your eyes' height above the floor now (the STAGE space), false: the runtime gives none.
bool FEyeAboveFloor(float* pm)
{
    if (!s.spaceStage || !s.spaceView || !s.fs.predictedDisplayTime) return false;
    XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
    if (XR_SUCCEEDED(xrLocateSpace(s.spaceView, s.spaceStage, s.fs.predictedDisplayTime, &loc)) &&
        (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) && loc.pose.position.y > 0.3f && loc.pose.position.y < 2.5f)
    {
        *pm = loc.pose.position.y;
        return true;
    }
    return false;
}

// The testing helpers (vr.h: the left stick click's chord, or the F8 keys)
enum class Helper { Reload, Recenter, HandCal, HeightCal };
void DoHelper(Helper h)
{
    switch (h)
    {
    case Helper::Reload:
        vrprofile::FLoad();
        std::printf("vr: settings reloaded\n");
        break;
    case Helper::Recenter:
        s.fRecenter = true;
        std::printf("vr: recentred\n");
        break;
    case Helper::HandCal:
        if (!s.fHandCal)
        {
            s.fHandCal = true;
            s.afHandAnim[0] = s.afHandAnim[1] = false;
            std::printf("vr: hand calibration: the hands stay where the game holds them; turn your hands to match, then "
                        "press it again (hold the left stick click + X, or Shift+F8)\n");
            break;
        }
        s.fHandCal = false;
        CaptureHands();
        break;
    case Helper::HeightCal:
        s.fRecenter = true;   // (your head now is the character's eye)
        if (float m = 0.0f; FEyeAboveFloor(&m))
        {
            char ach[32];
            std::snprintf(ach, sizeof ach, "%.3f", m);
            vrprofile::FSave("default", "player_eye_height", ach);
            std::printf("vr: height calibration: your eyes are %.2f m above the floor\n", m);
            break;
        }
        std::fprintf(stderr, "vr: height calibration: the runtime gave no floor height (recentred only; set the floor in "
                             "its own setup, or use scale in Sly2VR.ini)\n");
        break;
    }
}
// ---- the T-pose calibration (vr.h NoteTitle)
struct TPOSE
{
    glm::mat3 amatGrip[2]{};   // each grip's turn in your body's axes (right, up, back)
    float mSpan = 0.0f;        // the grips' distance apart (m)
};
// Your pose now as a T-pose (LOCAL space: Y up, metres); false, with what to change (shown), when it is not one.
bool FTPoseNow(TPOSE* ptp, const char** pszWhy)
{
    *pszWhy = nullptr;
    if (!s.fViews || !s.afGrip[0] || !s.afGrip[1])
    {
        *pszWhy = "Keep both controllers in view of the headset";
        return false;
    }
    const glm::vec3 posHead = 0.5f * (Vec(s.aview[0].pose.position) + Vec(s.aview[1].pose.position));
    const glm::vec3 posL = Vec(s.aposeGrip[0].position), posR = Vec(s.aposeGrip[1].position);
    glm::vec3 d = posR - posL;
    d.y = 0.0f;
    const float mSpan = glm::length(d);
    // (a short child's arms still span 0.9 m between the controllers)
    if (mSpan < 0.9f) { *pszWhy = "Stretch both arms straight out to your sides"; return false; }
    const glm::vec3 right = d / mSpan, up(0.0f, 1.0f, 0.0f), back = glm::cross(right, up);
    glm::vec3 fwd = Quat(s.aview[0].pose.orientation) * glm::vec3(0.0f, 0.0f, -1.0f);
    fwd.y = 0.0f;
    if (glm::length(fwd) < 1e-3f || glm::dot(glm::normalize(fwd), -back) < 0.8f)
    {
        *pszWhy = "Face straight ahead, between your hands";
        return false;
    }
    if (std::fabs(posL.y - posR.y) > 0.2f) { *pszWhy = "Keep both arms level"; return false; }
    const float yHands = 0.5f * (posL.y + posR.y);
    if (yHands > posHead.y + 0.05f || yHands < posHead.y - 0.6f) { *pszWhy = "Arms level with your shoulders"; return false; }
    if (std::fabs(glm::dot(0.5f * (posL + posR) - posHead, back)) > 0.35f)
    {
        *pszWhy = "Arms out to the sides, not forward";
        return false;
    }
    const glm::mat3 matBody(right, up, back);
    for (int k = 0; k < 2; ++k) ptp->amatGrip[k] = glm::transpose(matBody) * glm::mat3_cast(Quat(s.aposeGrip[k].orientation));
    ptp->mSpan = mSpan;
    return true;
}

// Saved into Sly2VR.ini's [default]: the grips' turns, the span, your eye height (with a floor-level space); a
// character's own hand turn (an older hand calibration) gives way to the T-pose's.
void SaveTPose(const TPOSE& tp)
{
    char ach[96];
    for (int k = 0; k < 2; ++k)
    {
        const glm::quat q = glm::normalize(glm::quat_cast(tp.amatGrip[k]));
        std::snprintf(ach, sizeof ach, "%.5f,%.5f,%.5f,%.5f", q.w, q.x, q.y, q.z);
        vrprofile::FSave("default", k ? "tpose_right" : "tpose_left", ach);
    }
    std::snprintf(ach, sizeof ach, "%.2f", tp.mSpan);
    vrprofile::FSave("default", "tpose_span", ach);
    float m = 0.0f;
    const bool fEye = FEyeAboveFloor(&m);
    if (fEye)
    {
        std::snprintf(ach, sizeof ach, "%.3f", m);
        vrprofile::FSave("default", "player_eye_height", ach);
    }
    for (vrprofile::Char c : {vrprofile::Char::Sly, vrprofile::Char::Bentley, vrprofile::Char::Murray})
    {
        const bool fOwnR = vrprofile::Profile(c).fHandOwnR, fOwnL = vrprofile::Profile(c).fHandOwnL;
        if (fOwnR) vrprofile::FSave(vrprofile::SzChar(c), "hand_rot", "auto");
        if (fOwnL) vrprofile::FSave(vrprofile::SzChar(c), "hand_rot_left", "auto");
    }
    if (fEye) std::printf("vr: T-pose saved (arms %.2f m apart, eyes %.2f m above the floor)\n", tp.mSpan, m);
    else std::printf("vr: T-pose saved (arms %.2f m apart; the runtime gave no floor height: eye height not set)\n", tp.mSpan);
}

void StartTPose()
{
    s.fTPoseAsked = true;
    s.fChord = s.fChordUsed = false;   // (a left stick click held into the pose starts nothing after it)
    s.tposeStep = 1;
    s.tTPoseHold = -1.0;
    s.tTPoseGrip = TNow();
    s.szTPoseHint = nullptr;
    std::printf("vr: T-pose calibration asked for\n");
}

// Each controller read while it is asked for: both triggers held, in a T-pose, still for kDtTPose seconds
void StepTPose(const bool* af, double t)
{
    using namespace vrprofile;
    TPOSE tp;
    const char* szWhy = nullptr;
    if (!af[Ctl_TriggerL] || !af[Ctl_TriggerR])
    {
        s.tTPoseHold = -1.0;
        s.szTPoseHint = nullptr;
        return;
    }
    if (!FTPoseNow(&tp, &szWhy))
    {
        s.tTPoseHold = -1.0;
        s.szTPoseHint = szWhy;
        return;
    }
    const glm::vec3 apos[2]{Vec(s.aposeGrip[0].position), Vec(s.aposeGrip[1].position)};
    const bool fMoved = s.tTPoseHold >= 0.0 &&
                        (glm::length(apos[0] - s.aposTPose[0]) > 0.08f || glm::length(apos[1] - s.aposTPose[1]) > 0.08f);
    if (s.tTPoseHold < 0.0 || fMoved)
    {
        s.tTPoseHold = t;
        s.aposTPose[0] = apos[0];
        s.aposTPose[1] = apos[1];
        s.szTPoseHint = fMoved ? "Hold still" : nullptr;
        return;
    }
    if (t - s.tTPoseHold < kDtTPose) return;
    SaveTPose(tp);
    s.fTPoseWanted = false;
    s.tposeStep = 2;
    s.tTPoseDone = t;
    s.fRecenter = true;   // (the way you faced is forward)
}

// ---- (0.1.2) the physical interactions (vr.h): LOCAL space throughout (metres, Y up), as the poses are read
constexpr int kcstepGripPending = 5;   // (a grip's new press at something to grab offered to FTakeGripPress this long)
constexpr int kcstepGripOffer = 2;     // (one elsewhere: offered, and its button held back, this many steps)
constexpr int kcstepClimbStale = 6;    // (SetClimbStick not set again for this many reads: the stick let go)
constexpr uint16_t kgrfAttack = BTN_Square | BTN_Triangle;
constexpr float kmGlideBelowHead = 0.45f;   // (the paraglider's lines: both hands above your head less this)

// You as a bigger or smaller Sly (sly_size): the world's scale and your eye height follow.
float SlySize() { return std::clamp(Prof().slySize, 0.05f, 50.0f); }   // (0.1.4: any size, no 0.5..2 cap)

glm::vec3 PosHeadLocal() { return 0.5f * (Vec(s.aview[0].pose.position) + Vec(s.aview[1].pose.position)); }
// The head's yaw in LOCAL (radians, + to the left; not recentred, unlike RadHeadYaw)
float YawHeadLocal()
{
    const glm::vec3 fwd = Quat(s.aview[0].pose.orientation) * glm::vec3(0.0f, 0.0f, -1.0f);
    return std::atan2(-fwd.x, -fwd.z);
}

// A point / a frame of LOCAL space in the world for the camera matrix matCam (MatCamOf), as MatTrackedLocal: from the
// recentred origin, the position scaled, the turn kept (no scale in it)
glm::vec3 PosWorldOfLocal(const glm::mat4& matCam, const glm::vec3& pos)
{
    const glm::quat qUnyaw = glm::angleAxis(-s.yawOrigin, glm::vec3(0.0f, 1.0f, 0.0f));
    return glm::vec3(matCam * glm::vec4(qUnyaw * (pos - s.posOrigin) * s.scale, 1.0f));
}
glm::mat4 MatWorldOfLocal(const glm::mat4& matCam, const glm::vec3& pos, const glm::mat3& mat)
{
    const glm::quat qUnyaw = glm::angleAxis(-s.yawOrigin, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 m(glm::mat3_cast(qUnyaw) * mat);
    m[3] = glm::vec4(qUnyaw * (pos - s.posOrigin) * s.scale, 1.0f);
    return matCam * m;
}

// The torso's axes in LOCAL for its yaw: right, up, back (as GL eye axes)
glm::mat3 MatTorso(float yaw)
{
    return glm::mat3(glm::vec3(std::cos(yaw), 0.0f, -std::sin(yaw)), glm::vec3(0.0f, 1.0f, 0.0f),
                     glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw)));
}

// The body's yaw in LOCAL, with no torso tracking: the head's, dragged along at once past 40 degrees away and drawn
// toward it slowly otherwise (a body turned in place catches up in a second or two); each controller read
constexpr float kdegTorsoLag = 40.0f;
constexpr double kdtTorsoFollow = 1.5;
void StepTorso(double t)
{
    if (!s.fViews) return;
    const glm::vec3 fwd = Quat(s.aview[0].pose.orientation) * glm::vec3(0.0f, 0.0f, -1.0f);
    if (fwd.x * fwd.x + fwd.z * fwd.z > 0.1f)   // (looking straight up or down: no yaw to follow)
    {
        const float yawHead = std::atan2(-fwd.x, -fwd.z);
        if (!s.fTorso || t - s.tTorso > 1.0) s.yawTorso = yawHead, s.fTorso = true;   // (first, or after a gap)
        float d = std::remainder(yawHead - s.yawTorso, 2.0f * glm::pi<float>());
        const float dMax = glm::radians(kdegTorsoLag);
        if (std::fabs(d) > dMax) s.yawTorso += d - std::copysign(dMax, d), d = std::copysign(dMax, d);
        s.yawTorso = std::remainder(s.yawTorso + d * float(std::min(1.0, (t - s.tTorso) / kdtTorsoFollow)), 2.0f * glm::pi<float>());
    }
    s.tTorso = t;
}
float YawTorso() { return s.fTorso ? s.yawTorso : YawHeadLocal(); }

// The holster zone behind the right shoulder (vr.h FCaneDrawn): the right grip right, up and back of the eyes' middle
// in the torso's axes, within these (metres); a little larger once in it (hysteresis)
constexpr float kxHolster0 = 0.0f, kxHolster1 = 0.38f, kyHolster0 = -0.38f, kyHolster1 = 0.12f;
constexpr float kzHolster0 = 0.05f, kzHolster1 = 0.38f, kmHolsterHyst = 0.05f;
bool FRightHandAtHolster()
{
    if (!s.fViews || !s.afGrip[1]) return false;
    const glm::vec3 d = glm::transpose(MatTorso(YawTorso())) * (Vec(s.aposeGrip[1].position) - PosHeadLocal());
    const float e = s.fAtHolster ? kmHolsterHyst : 0.0f;
    return d.x > kxHolster0 - e && d.x < kxHolster1 + e && d.y > kyHolster0 - e && d.y < kyHolster1 + e &&
           d.z > kzHolster0 - e && d.z < kzHolster1 + e;
}

// The holster's frame (FHolsterWorld): where the cane's grip point rests, right of, below and behind the eyes' middle
// in the torso's axes; -Z along the cane toward its hook: worn like a sword on the back, the hand's end over the right
// shoulder and the crook (the longer part, about 0.9 m) hanging down behind the back toward the left hip, never in
// front of you; +Y toward your front, so the crook (on the cane's -Y side) curls away behind you. (The torso's yaw as
// of the last read: nothing changes at draw time.)
constexpr float kxHolster = 0.17f, kyHolster = -0.12f, kzHolster = 0.18f;
bool FHolsterLocal(glm::vec3* ppos, glm::mat3* pmat)
{
    if (!s.fViews) return false;
    const glm::mat3 mT = MatTorso(YawTorso());
    *ppos = PosHeadLocal() + mT * glm::vec3(kxHolster, kyHolster, kzHolster);
    const glm::vec3 z = -glm::normalize(mT * glm::vec3(-0.25f, -1.0f, 0.15f));
    const glm::vec3 y = -glm::normalize(mT[2] - glm::dot(mT[2], z) * z);
    *pmat = glm::mat3(glm::cross(y, z), y, z);
    return true;
}

// The wristwatch (vr.h FWatchOpen) in the right grip's space (OpenXR's grip: +X out of the back of the right hand, so
// its palm faces -X; +Y toward the wrist; -Z the thumb's side): on the inside of the wrist, a little off it; the
// picture's right along the forearm (toward the elbow), its up toward the thumb, its face out of the palm side: upright
// to your eyes with the forearm across you and the palm turned to you. SLY2N_VR_WATCH_FLIP=1: turned 180 degrees in
// its plane (a test fallback, should it read upside down).
const glm::vec3 kposWatchGrip(-0.03f, 0.08f, 0.0f);
constexpr float kdxWatch = 0.13f, kdyWatch = 0.0975f;           // (metres: 4:3, as the canvas)
constexpr int kdxWatchImage = 768, kdyWatchImage = 576;         // (its swapchain's pixels)
constexpr float kdegWatchOpen = 40.0f, kdegWatchStay = 50.0f;   // (the palm side's angle off the way to your eyes)
constexpr float kmWatchReach = 0.65f;                           // (the watch within this of your head)
constexpr double kdtWatchOpen = 0.15, kdtWatchClose = 0.3;      // (the debounce)
glm::quat QuatWatchGrip()
{
    static const glm::quat s_q = [] {
        const glm::quat q = glm::quat_cast(glm::mat3(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(-1.0f, 0.0f, 0.0f)));
        return FloatEnv("SLY2N_VR_WATCH_FLIP", 0.0f) != 0.0f ? q * glm::angleAxis(glm::pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f)) : q;
    }();
    return s_q;
}
// The watch's centre and face in LOCAL (columns: the picture's right, its up, its normal out of the face); false: the
// right hand not tracked
bool FWatchLocal(glm::vec3* ppos, glm::mat3* pmat)
{
    if (!s.afGrip[1]) return false;
    const glm::quat q = Quat(s.aposeGrip[1].orientation);
    *ppos = Vec(s.aposeGrip[1].position) + q * kposWatchGrip;
    *pmat = glm::mat3_cast(q * QuatWatchGrip());
    return true;
}

// Each controller read: the watch open while the inside of the right wrist faces your eyes (debounced); the left
// hand's laser on its face picks out an entry (NoteAimUsed draws the laser), the left trigger's press picks it (that
// press is no L1 until let go, over an entry or not)
void StepWatch(bool fAllowed, bool fTriggerEdge, double t)
{
    glm::vec3 posWatch(0.0f);
    glm::mat3 matWatch(1.0f);
    const bool fWatch = fAllowed && s.fViews && FWatchLocal(&posWatch, &matWatch);
    bool fFacing = false;
    if (fWatch)
    {
        const glm::vec3 d = PosHeadLocal() - posWatch;
        const float m = glm::length(d);
        const float cosMin = std::cos(glm::radians(s.fWatchOpen ? kdegWatchStay : kdegWatchOpen));
        fFacing = m > 0.05f && m < kmWatchReach && glm::dot(matWatch[2], d / m) > cosMin;
    }
    if (!fAllowed) s.fWatchOpen = false, s.tWatchSteady = t;
    else if (fFacing == s.fWatchOpen) s.tWatchSteady = t;
    else if (t - s.tWatchSteady >= (fFacing ? kdtWatchOpen : kdtWatchClose)) s.fWatchOpen = fFacing, s.tWatchSteady = t;
    s.iWatchHover = -1;
    if (s.fPointerLocal) s.cframePointer = 0;   // (the watch's laser: only while this read finds it pointing)
    if (!s.fWatchOpen || !fWatch) return;
    if (s.afAim[0])
    {
        // the aim ray onto the face's plane, from its front; (u, v) over the canvas, v down
        const glm::vec3 o = Vec(s.aposeAim[0].position);
        const glm::vec3 dir = Quat(s.aposeAim[0].orientation) * glm::vec3(0.0f, 0.0f, -1.0f);
        const float dn = glm::dot(dir, matWatch[2]);
        const float tHit = dn < -1e-3f ? glm::dot(posWatch - o, matWatch[2]) / dn : -1.0f;
        if (tHit > 0.0f && tHit < 2.0f)
        {
            const glm::vec3 p = o + dir * tHit;
            const float u = glm::dot(p - posWatch, matWatch[0]) / kdxWatch + 0.5f;
            const float v = 0.5f - glm::dot(p - posWatch, matWatch[1]) / kdyWatch;
            if (u > -0.05f && u < 1.05f && v > -0.05f && v < 1.05f)
            {
                s.iWatchHover = IWatchEntryAt(u, v, int(s.astrWatch.size()));
                s.posPtrFromLocal = o;
                s.posPtrToLocal = p;
                s.fPointerLocal = true;
                s.cframePointer = 4;   // (only while it points at the watch: kept over the frames between steps, 144 Hz)
            }
        }
    }
    if (fTriggerEdge)
    {
        s.afCtlChord[vrprofile::Ctl_TriggerL] = true;
        if (s.iWatchHover >= 0)
        {
            s.iWatchPick = s.iWatchHover;
            Pulse(0, 0.5f, 0.04f);
        }
    }
}

// The interactions' passing state let go (not first person at the head, a menu, the T-pose)
void DropInteractions()
{
    s.fAtHolster = s.fWatchOpen = false;
    s.iWatchHover = s.iWatchPick = -1;
    if (s.fPointerLocal) s.cframePointer = 0;
    for (int k = 0; k < 2; ++k) s.afGripPending[k] = false, s.acstepGripPending[k] = 0;
    std::fill(std::begin(s.acstepReq), std::end(s.acstepReq), 0);
    s.fClimbStick = false;
}

// Each controller read: both grips' velocities (LOCAL, m/s: the swing, FGripVelocityLast), at the last frame's time
// as the poses
void ReadGripVelocities()
{
    for (int k = 0; k < 2; ++k)
    {
        s.afVelGrip[k] = false;
        if (!s.aspaceGrip[k] || !s.fs.predictedDisplayTime) continue;
        XrSpaceVelocity vel{XR_TYPE_SPACE_VELOCITY};
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        loc.next = &vel;
        if (XR_SUCCEEDED(xrLocateSpace(s.aspaceGrip[k], s.space, s.fs.predictedDisplayTime, &loc)) &&
            (vel.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT))
        {
            s.avelGripLocal[k] = Vec(vel.linearVelocity);
            s.afVelGrip[k] = true;
        }
    }
}
}  // namespace

void PreInit()
{
#if !defined(_WIN32) && defined(GLFW_PLATFORM)
    // OpenXR's OpenGL binding on Linux is GLX: GLFW 3.4 would pick Wayland (EGL) on a Wayland desktop. (The checkout's
    // Libraries/include/GLFW/glfw3.h is found before the fetched 3.4's and has no GLFW_PLATFORM: there, run the game
    // under X11 / XWayland yourself, e.g. with WAYLAND_DISPLAY unset.)
    if (std::getenv("SLY2N_VR")) glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
}

bool Start(GLFWwindow* window)
{
    const char* szMode = std::getenv("SLY2N_VR");
    if (!szMode || !*szMode) return false;
    if (!std::strcmp(szMode, "screen")) s.mode = Mode::Screen;
    else if (!std::strcmp(szMode, "stereo") || !std::strcmp(szMode, "1")) s.mode = Mode::Stereo;
    else if (!std::strcmp(szMode, "fp")) s.mode = Mode::FirstPerson;
    else
    {
        std::fprintf(stderr, "vr: SLY2N_VR=%s: use screen, stereo or fp\n", szMode);
        return false;
    }
    s.fLevel = FloatEnv("SLY2N_VR_LEVEL", 1.0f) != 0.0f;
    s.dPanel = std::max(0.3f, FloatEnv("SLY2N_VR_SCREEN", s.mode == Mode::Screen ? 3.0f : 1.5f));
    s.dScreen = s.mode == Mode::Screen ? s.dPanel : 3.0f;
    vrprofile::FLoad();   // (Sly2VR.ini: the per-character settings; written out with the defaults the first time)
    s.fCalForced = FloatEnv("SLY2N_VR_CALIBRATE", 0.0f) != 0.0f;   // (the package's Calibrate launcher)
    s.scale = Prof().scale * SlySize();   // (until SetCharacter's)

    const char* aszExt[] = {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo ici{XR_TYPE_INSTANCE_CREATE_INFO};
    std::snprintf(ici.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "Sly 2 SafeHouse VR");
    std::snprintf(ici.applicationInfo.engineName, XR_MAX_ENGINE_NAME_SIZE, "SafeHouse");
    ici.applicationInfo.applicationVersion = 1;
    ici.applicationInfo.apiVersion = XR_API_VERSION_1_0;   // (1.0: what every runtime takes)
    ici.enabledExtensionCount = 1;
    ici.enabledExtensionNames = aszExt;
    if (!FCheck(xrCreateInstance(&ici, &s.inst), "xrCreateInstance (is a VR runtime installed and running?)")) { Stop(); return false; }

    XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO};
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!FCheck(xrGetSystem(s.inst, &sgi, &s.sys), "xrGetSystem (is the headset connected?)")) { Stop(); return false; }

    // (required before the session: the runtime's OpenGL version range)
    PFN_xrGetOpenGLGraphicsRequirementsKHR pfnReq = nullptr;
    if (!FCheck(xrGetInstanceProcAddr(s.inst, "xrGetOpenGLGraphicsRequirementsKHR", reinterpret_cast<PFN_xrVoidFunction*>(&pfnReq)),
                "xrGetInstanceProcAddr") || !pfnReq)
    {
        Stop();
        return false;
    }
    XrGraphicsRequirementsOpenGLKHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
    if (!FCheck(pfnReq(s.inst, s.sys, &req), "xrGetOpenGLGraphicsRequirementsKHR")) { Stop(); return false; }
    GLint glMajor = 0, glMinor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &glMajor);
    glGetIntegerv(GL_MINOR_VERSION, &glMinor);
    if (XR_MAKE_VERSION(glMajor, glMinor, 0) < req.minApiVersionSupported)
    {
        std::fprintf(stderr, "vr: the runtime needs OpenGL %d.%d, the context is %d.%d\n", int(XR_VERSION_MAJOR(req.minApiVersionSupported)),
                     int(XR_VERSION_MINOR(req.minApiVersionSupported)), glMajor, glMinor);
        Stop();
        return false;
    }

    // the game's current GL context
#if defined(_WIN32)
    XrGraphicsBindingOpenGLWin32KHR gb{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
    gb.hDC = wglGetCurrentDC();
    gb.hGLRC = wglGetCurrentContext();
    if (!gb.hDC || !gb.hGLRC)
    {
        std::fprintf(stderr, "vr: no current WGL context\n");
        Stop();
        return false;
    }
#else
    XrGraphicsBindingOpenGLXlibKHR gb{XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR};
    gb.xDisplay = glXGetCurrentDisplay();
    gb.glxContext = glXGetCurrentContext();
    gb.glxDrawable = glXGetCurrentDrawable();
    if (!gb.xDisplay || !gb.glxContext)
    {
        std::fprintf(stderr, "vr: no current GLX context (a Wayland session without X11?)\n");
        Stop();
        return false;
    }
    {
        int idConfig = 0;
        glXQueryContext(gb.xDisplay, gb.glxContext, GLX_FBCONFIG_ID, &idConfig);
        const int aattr[] = {GLX_FBCONFIG_ID, idConfig, 0};
        int cConfig = 0;
        GLXFBConfig* pconfig = glXChooseFBConfig(gb.xDisplay, DefaultScreen(gb.xDisplay), aattr, &cConfig);
        if (pconfig && cConfig > 0)
        {
            gb.glxFBConfig = pconfig[0];
            if (XVisualInfo* pvi = glXGetVisualFromFBConfig(gb.xDisplay, pconfig[0]))
            {
                gb.visualid = uint32_t(pvi->visualid);
                XFree(pvi);
            }
        }
        if (pconfig) XFree(pconfig);
    }
#endif
    // (the controllers: no actions is not fatal, the gamepad and keyboard still play)
    const bool fActionsMade = FCreateActions();
    XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
    sci.next = &gb;
    sci.systemId = s.sys;
    if (!FCheck(xrCreateSession(s.inst, &sci, &s.sess), "xrCreateSession")) { Stop(); return false; }
    s.fActions = fActionsMade && FAttachActions();

    XrReferenceSpaceCreateInfo rci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;   // (seated: the origin where the head was at the start)
    rci.poseInReferenceSpace.orientation.w = 1.0f;
    if (!FCheck(xrCreateReferenceSpace(s.sess, &rci, &s.space), "xrCreateReferenceSpace")) { Stop(); return false; }
    // (the height calibration: the head's height above the floor, where the runtime has a floor-level space)
    rci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    if (XR_FAILED(xrCreateReferenceSpace(s.sess, &rci, &s.spaceStage))) s.spaceStage = XR_NULL_HANDLE;
    rci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    if (XR_FAILED(xrCreateReferenceSpace(s.sess, &rci, &s.spaceView))) s.spaceView = XR_NULL_HANDLE;

    // the swapchains' format: sRGB, so the runtime takes the game's (gamma encoded) bytes as they are
    {
        uint32_t c = 0;
        xrEnumerateSwapchainFormats(s.sess, 0, &c, nullptr);
        std::vector<int64_t> aformat(c);
        xrEnumerateSwapchainFormats(s.sess, c, &c, aformat.data());
        for (int64_t want : {int64_t(GL_SRGB8_ALPHA8), int64_t(GL_RGBA8)})
            if (std::find(aformat.begin(), aformat.end(), want) != aformat.end()) { s.format = want; break; }
        if (!s.format)
        {
            std::fprintf(stderr, "vr: the runtime offers neither GL_SRGB8_ALPHA8 nor GL_RGBA8 swapchains\n");
            Stop();
            return false;
        }
    }
    if (s.mode == Mode::Stereo || s.mode == Mode::FirstPerson)
    {
        uint32_t c = 0;
        xrEnumerateViewConfigurationViews(s.inst, s.sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &c, nullptr);
        std::vector<XrViewConfigurationView> aview(c, XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW});
        xrEnumerateViewConfigurationViews(s.inst, s.sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, c, &c, aview.data());
        if (c != 2)
        {
            std::fprintf(stderr, "vr: %u views (two expected)\n", c);
            Stop();
            return false;
        }
        for (int k = 0; k < 2; ++k)
            if (!FCreateSwap(int(aview[k].recommendedImageRectWidth), int(aview[k].recommendedImageRectHeight), &s.aeye[k])) { Stop(); return false; }
    }
    if (!FCreateSwap(std::max(g_gl.renderWidth, 640), std::max(g_gl.renderHeight, 360), &s.panel)) { Stop(); return false; }
    // (first person: the wristwatch's picture, the 4:3 canvas, for its quad on the right wrist; none: no watch shown)
    if (s.mode == Mode::FirstPerson && !FCreateSwap(kdxWatchImage, kdyWatchImage, &s.watch))
    {
        DestroySwap(&s.watch);
        std::fprintf(stderr, "vr: the wristwatch's swapchain was not made (the watch is not shown)\n");
    }
    glGenFramebuffers(1, &s.fboBlit);
    // xrWaitFrame paces the frames now: the window's swap must not wait for the monitor too (display.cpp
    // ApplyDisplaySettings keeps it so while a session exists, and Stop() gives the user's VSync back)
    if (window) ApplyDisplaySettings(window);
    std::printf("vr: started (%s mode, scale %.1f units/m%s%s)\n",
                s.mode == Mode::FirstPerson ? "first-person" : s.mode == Mode::Stereo ? "stereo" : "screen", s.scale,
                s.fLevel ? ", level horizon" : "", s.fActions ? ", motion controllers" : ", no motion controllers");
    return true;
}

void Stop()
{
    const bool fHadSession = s.sess != XR_NULL_HANDLE;
    for (XrSpace& sp : s.aspaceGrip)
        if (sp) xrDestroySpace(sp);
    for (XrSpace& sp : s.aspaceAim)
        if (sp) xrDestroySpace(sp);
    if (s.spaceStage) xrDestroySpace(s.spaceStage);
    if (s.spaceView) xrDestroySpace(s.spaceView);
    if (s.fboMirror) glDeleteFramebuffers(1, &s.fboMirror);
    if (s.texMirror) glDeleteTextures(1, &s.texMirror);
    if (s.fboMirrorHud) glDeleteFramebuffers(1, &s.fboMirrorHud);
    if (s.texMirrorHud) glDeleteTextures(1, &s.texMirrorHud);
    if (s.progQuad) glDeleteProgram(s.progQuad);
    if (s.vboPointer) glDeleteBuffers(1, &s.vboPointer);
    if (s.vaoPointer) glDeleteVertexArrays(1, &s.vaoPointer);
    if (s.progPointer) glDeleteProgram(s.progPointer);
    if (s.actionset) xrDestroyActionSet(s.actionset);   // (its actions with it)
    for (SWAP& sw : s.aeye) DestroySwap(&sw);
    DestroySwap(&s.panel);
    DestroySwap(&s.watch);
    if (s.fboBlit) glDeleteFramebuffers(1, &s.fboBlit);
    if (s.space) xrDestroySpace(s.space);
    if (s.sess) xrDestroySession(s.sess);
    if (s.inst) xrDestroyInstance(s.inst);
    s = STATE{};
    // (the window's own VSync and pacing back: ApplyDisplaySettings follows vr::Active(), now false)
    if (fHadSession && g_gl.window) ApplyDisplaySettings(g_gl.window);
}

bool Active() { return s.sess != XR_NULL_HANDLE; }
bool FPacesFrames() { return s.sess != XR_NULL_HANDLE && s.fRunning; }
Mode CurrentMode() { return s.sess ? s.mode : Mode::Off; }

bool BeginFrame(GLFWwindow* window)
{
    if (!s.sess) return false;
    PollEvents();
    if (s.fLost)
    {
        std::fprintf(stderr, "vr: the session ended; the game goes on in the window\n");
        Stop();
        return false;
    }
    // F8 recentres; Ctrl+F8 reloads Sly2VR.ini, Shift+F8 the hand calibration, Alt+F8 the height calibration (F8 is
    // the VR's alone; the game's debug keys use the others)
    const bool fF8 = window && glfwGetKey(window, GLFW_KEY_F8) == GLFW_PRESS;
    if (fF8 && !s.fF8Was)
    {
        auto held = [&](int a, int b) { return glfwGetKey(window, a) == GLFW_PRESS || glfwGetKey(window, b) == GLFW_PRESS; };
        if (held(GLFW_KEY_LEFT_CONTROL, GLFW_KEY_RIGHT_CONTROL)) DoHelper(Helper::Reload);
        else if (held(GLFW_KEY_LEFT_SHIFT, GLFW_KEY_RIGHT_SHIFT)) DoHelper(Helper::HandCal);
        else if (held(GLFW_KEY_LEFT_ALT, GLFW_KEY_RIGHT_ALT)) DoHelper(Helper::HeightCal);
        else DoHelper(Helper::Recenter);
    }
    s.fF8Was = fF8;
    if (!s.fRunning) return false;

    XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
    s.fs = XrFrameState{XR_TYPE_FRAME_STATE};
    if (!FCheck(xrWaitFrame(s.sess, &wi, &s.fs), "xrWaitFrame")) return false;
    XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
    if (!FCheck(xrBeginFrame(s.sess, &bi), "xrBeginFrame")) return false;
    s.fBegun = true;
    s.afEye[0] = s.afEye[1] = s.fPanel = s.fPanelScreen = false;
    s.fShould = s.fs.shouldRender == XR_TRUE;
    s.fViews = false;
    if (!s.fShould) return false;

    XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = s.fs.predictedDisplayTime;
    li.space = s.space;
    XrViewState vs{XR_TYPE_VIEW_STATE};
    s.aview[0] = s.aview[1] = XrView{XR_TYPE_VIEW};
    uint32_t c = 0;
    if (FCheck(xrLocateViews(s.sess, &li, &vs, 2, &c, s.aview), "xrLocateViews"))
        s.fViews = c == 2 && (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT);
    for (int k = 0; k < 2; ++k)
    {
        auto locate = [&](XrSpace space, XrPosef* ppose, bool* pf) {
            *pf = false;
            if (!space) return;
            XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
            if (XR_SUCCEEDED(xrLocateSpace(space, s.space, s.fs.predictedDisplayTime, &loc)) &&
                (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) && (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
            {
                *ppose = loc.pose;
                *pf = true;
            }
        };
        locate(s.aspaceGrip[k], &s.aposeGrip[k], &s.afGrip[k]);
        locate(s.aspaceAim[k], &s.aposeAim[k], &s.afAim[k]);
    }
    // (FAimRay: the right controller counts as in use once it moves 2 cm or turns 3 degrees from where it rested)
    if (s.afAim[1])
    {
        const XrPosef& p = s.aposeAim[1];
        const bool fMoved = !s.fAimRef || glm::length(Vec(p.position) - Vec(s.poseAimRef.position)) > 0.02f ||
                            std::fabs(glm::dot(Quat(p.orientation), Quat(s.poseAimRef.orientation))) < std::cos(glm::radians(1.5f));
        if (fMoved)
        {
            if (s.fAimRef) s.tRightActive = TNow();
            s.poseAimRef = p;
            s.fAimRef = true;
        }
    }
    if (s.fViews && s.fRecenter)
    {
        // the head between the eyes; forward is the left eye's -Z, flattened
        s.posOrigin = 0.5f * (Vec(s.aview[0].pose.position) + Vec(s.aview[1].pose.position));
        const glm::vec3 fwd = Quat(s.aview[0].pose.orientation) * glm::vec3(0.0f, 0.0f, -1.0f);
        s.yawOrigin = std::atan2(-fwd.x, -fwd.z);
        s.fRecenter = false;
    }
    return true;
}

bool EyeView(int k, const CAMERA& cam, VIEWOVERRIDE* pview)
{
    if (!s.fViews || !FEyes() || k < 0 || k > 1) return false;
    const XrView& v = s.aview[k];
    s.matCamLast = MatCamOf(cam);   // (FAimRay: the controllers in the world the eyes were drawn in)
    s.fCamLast = true;
    const glm::mat4 matEye = s.matCamLast * MatTrackedLocal(v.pose);

    // the eye's own (asymmetric) field of view, reversed-Z infinite far as CAMERA::MatWorldToClip
    const float l = std::tan(v.fov.angleLeft), r = std::tan(v.fov.angleRight);
    const float d = std::tan(v.fov.angleDown), u = std::tan(v.fov.angleUp);
    glm::mat4 proj(0.0f);
    proj[0][0] = 2.0f / (r - l);
    proj[1][1] = 2.0f / (u - d);
    proj[2][0] = (r + l) / (r - l);
    proj[2][1] = (u + d) / (u - d);
    proj[2][3] = -1.0f;
    // (first person: the hands and the cane are within the game camera's 100-unit near plane: 5 cm instead)
    proj[3][2] = FFirstPersonEyes() ? std::min(cam.sNear, 0.05f * s.scale) : cam.sNear;

    // (FHeadView: once both eyes of this frame are known, the head between them with their combined field of view)
    s.amatEyeWorld[k] = matEye;
    s.afEyeWorld[k] = true;
    if (s.afEyeWorld[0] && s.afEyeWorld[1])
    {
        const glm::mat4& m = s.amatEyeWorld[0];
        s.posHead = 0.5f * (glm::vec3(s.amatEyeWorld[0][3]) + glm::vec3(s.amatEyeWorld[1][3]));
        s.matHeadRows[0] = glm::normalize(-glm::vec3(m[2]));   // forward (GL eye: back is +z)
        s.matHeadRows[1] = glm::normalize(-glm::vec3(m[0]));   // left
        s.matHeadRows[2] = glm::normalize(glm::vec3(m[1]));    // up
        const XrFovf& fl = s.aview[0].fov;
        const XrFovf& fr = s.aview[1].fov;
        s.tanHeadX = std::max({std::tan(fr.angleRight), -std::tan(fl.angleLeft), 0.1f});
        s.tanHeadY = std::max({std::tan(fl.angleUp), std::tan(fr.angleUp), -std::tan(fl.angleDown), -std::tan(fr.angleDown), 0.1f});
        s.fHead = true;
        ++s.serialHead;
        s.afEyeWorld[0] = s.afEyeWorld[1] = false;
    }
    pview->matWorldToClip = proj * glm::inverse(matEye);
    pview->pos = glm::vec3(matEye[3]);
    pview->dir = -glm::vec3(matEye[2]);
    pview->fSkipTargets = k > 0;
    return true;
}

void SubmitEye(int k)
{
    if (!s.fShould || k < 0 || k > 1 || !s.aeye[k].h) return;
    BlitSceneInto(s.aeye[k]);
    if (k == 0) KeepMirror();   // (the resolved left eye kept for the desktop window)
    XrCompositionLayerProjectionView& pv = s.aprojv[k];
    pv = XrCompositionLayerProjectionView{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
    pv.pose = s.aview[k].pose;   // (the pose the eye was drawn from, not the recentred one)
    pv.fov = s.aview[k].fov;
    pv.subImage.swapchain = s.aeye[k].h;
    pv.subImage.imageRect.offset = {0, 0};
    pv.subImage.imageRect.extent = {s.aeye[k].dx, s.aeye[k].dy};
    pv.subImage.imageArrayIndex = 0;
    s.afEye[k] = true;
}

void BeginHudLayer()
{
    g_fHudWritesAlpha = true;   // (until SubmitPanel: the panel's alpha is the HUD's coverage, premultiplied)
    glBindFramebuffer(GL_FRAMEBUFFER, g_gl.FboScene());
    GLboolean amask[4];
    glGetBooleanv(GL_COLOR_WRITEMASK, amask);
    const GLboolean fScissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(amask[0], amask[1], amask[2], amask[3]);
    if (fScissor) glEnable(GL_SCISSOR_TEST);
}

void SubmitPanel(bool fScreen)
{
    g_fHudWritesAlpha = false;
    if (!s.fShould || !s.panel.h) return;
    BlitSceneInto(s.panel);
    s.fPanel = true;
    s.fPanelScreen = fScreen;
}

void BeginWatchLayer()
{
    if (!s.sess) return;
    // (after SubmitPanel: the HUD it showed kept for the window's mirror, which would show the watch otherwise; call it
    // after the eyes and before BeginHudLayer, or after SubmitPanel, never between those two: the HUD would be lost)
    if (s.fPanel) KeepMirrorHud();
    BeginHudLayer();   // (the scene target cleared to transparent, the coverage into alpha, until SubmitWatch)
}

void SubmitWatch()
{
    g_fHudWritesAlpha = false;
    if (!s.fShould || !s.watch.h) return;
    // only the 4:3 canvas (display.h: pillarboxed or letterboxed in the scene target), as the watch's 4:3 picture
    const glm::vec2 sc = glm::clamp(g_display.uiScale, glm::vec2(0.1f), glm::vec2(1.0f));
    const int dx = g_gl.renderWidth, dy = g_gl.renderHeight;
    const int x0 = int(std::lround(float(dx) * (1.0f - sc.x) * 0.5f)), y0 = int(std::lround(float(dy) * (1.0f - sc.y) * 0.5f));
    BlitSceneInto(s.watch, x0, y0, dx - x0, dy - y0);
    s.fWatchFrame = true;
}

bool FPresentMirror()
{
    // (the HUD kept before the watch was drawn over the scene target: BeginWatchLayer after SubmitPanel)
    const bool fHudKept = std::exchange(s.fMirrorHudKept, false) && s.fboMirrorHud && s.texMirrorHud;
    if (fHudKept && NMirror() == 0)
    {
        // (mirror = hud: as GL::Present would show the scene target, from that copy)
        const DISPLAY& d = g_display;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, s.fboMirrorHud);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glBlitFramebuffer(0, 0, s.dxMirrorHud, s.dyMirrorHud, d.presentX, d.presentY, d.presentX + d.presentWidth,
                          d.presentY + d.presentHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        s.fMirrorFrame = false;
        return true;
    }
    if (!s.fMirrorFrame || !s.fboMirror) return false;
    s.fMirrorFrame = false;
    const int nMirror = NMirror();
    if (nMirror == 0) return false;
    // (as GL::Present: the scene target, now the HUD over transparent, resolved into g_gl.fbo / g_gl.fbc)
    if (g_gl.fboMsaa && !fHudKept)
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, g_gl.fboMsaa);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_gl.fbo);
        glBlitFramebuffer(0, 0, g_gl.renderWidth, g_gl.renderHeight, 0, 0, g_gl.renderWidth, g_gl.renderHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    const DISPLAY& d = g_display;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    // the eye's picture is its field of view stretched over the scene target: cropped back to the window's shape
    // (the middle of the eye, top and bottom or sides cut), so nothing looks stretched
    const float rEye = (std::tan(s.fovMirror.angleRight) - std::tan(s.fovMirror.angleLeft)) /
                       std::max(1e-3f, std::tan(s.fovMirror.angleUp) - std::tan(s.fovMirror.angleDown));
    const float rWin = float(d.presentWidth) / float(std::max(d.presentHeight, 1));
    float fx = 1.0f, fy = 1.0f;
    if (rEye > 0.0f && rEye < rWin) fy = rEye / rWin;
    else if (rEye > rWin) fx = rWin / rEye;
    const int sx0 = int(s.dxMirror * (1.0f - fx) * 0.5f), sx1 = s.dxMirror - sx0;
    const int sy0 = int(s.dyMirror * (1.0f - fy) * 0.5f), sy1 = s.dyMirror - sy0;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, s.fboMirror);
    glBlitFramebuffer(sx0, sy0, sx1, sy1, d.presentX, d.presentY, d.presentX + d.presentWidth, d.presentY + d.presentHeight,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    if (nMirror == 2 && FQuadProgram())
    {
        // the HUD over it (its own 4:3 canvas in the picture, as on a flat screen); the GL state put back after
        GLint prog = 0, vao = 0, tex = 0, aviewport[4]{}, srcRgb = 0, dstRgb = 0, srcA = 0, dstA = 0;
        const GLboolean fBlend = glIsEnabled(GL_BLEND), fDepth = glIsEnabled(GL_DEPTH_TEST), fCull = glIsEnabled(GL_CULL_FACE);
        glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
        GLint sampler = 0;   // (the glob pass leaves its own sampler objects on the units: fbc's parameters wanted)
        glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
        glBindSampler(0, 0);
        glGetIntegerv(GL_VIEWPORT, aviewport);
        glGetIntegerv(GL_BLEND_SRC_RGB, &srcRgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &dstRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcA);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &dstA);
        if (!s.vaoPointer) glGenVertexArrays(1, &s.vaoPointer);
        glViewport(d.presentX, d.presentY, d.presentWidth, d.presentHeight);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);   // (premultiplied: hud.cpp)
        glUseProgram(s.progQuad);
        glBindVertexArray(s.vaoPointer);
        glBindTexture(GL_TEXTURE_2D, fHudKept ? s.texMirrorHud : g_gl.fbc);
        glUniform1i(glGetUniformLocation(s.progQuad, "uTex"), 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindTexture(GL_TEXTURE_2D, GLuint(tex));
        glBindSampler(0, GLuint(sampler));
        glBindVertexArray(GLuint(vao));
        glUseProgram(GLuint(prog));
        glBlendFuncSeparate(GLenum(srcRgb), GLenum(dstRgb), GLenum(srcA), GLenum(dstA));
        glViewport(aviewport[0], aviewport[1], aviewport[2], aviewport[3]);
        if (!fBlend) glDisable(GL_BLEND);
        if (fDepth) glEnable(GL_DEPTH_TEST);
        if (fCull) glEnable(GL_CULL_FACE);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void EndFrame()
{
    if (s.cframePointer > 0) --s.cframePointer;
    const bool fWatchFrame = std::exchange(s.fWatchFrame, false);   // (SubmitWatch ran: the watch's quad shown)
    s.fMirrorHudKept = false;
    if (!s.fBegun) return;
    // (a frame without the headset's eyes: the head view and the aim's camera are stale from now on)
    if (!(s.fShould && s.afEye[0] && s.afEye[1])) s.fHead = s.fCamLast = false;
    s.fBegun = false;
    std::vector<const XrCompositionLayerBaseHeader*> alayer;
    XrCompositionLayerProjection lp{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    XrCompositionLayerQuad lq{XR_TYPE_COMPOSITION_LAYER_QUAD};
    if (s.fShould && s.afEye[0] && s.afEye[1] && !s.fPanelScreen)
    {
        lp.space = s.space;
        lp.viewCount = 2;
        lp.views = s.aprojv;
        alayer.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&lp));
    }
    if (s.fShould && s.fPanel)
    {
        // in front of the recentred origin, facing it: the big screen about 60 degrees wide (screen mode, a movie),
        // the HUD panel 50 over the per-eye world
        const bool fScreenLook = s.mode == Mode::Screen || s.fPanelScreen;
        const float d = fScreenLook ? s.dScreen : s.dPanel;
        const glm::quat q = glm::angleAxis(s.yawOrigin, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 pos = s.posOrigin + q * glm::vec3(0.0f, 0.0f, -d);
        const float w = 2.0f * d * std::tan(glm::radians(fScreenLook ? 30.0f : 25.0f));
        lq.space = s.space;
        lq.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        lq.subImage.swapchain = s.panel.h;
        lq.subImage.imageRect.offset = {0, 0};
        lq.subImage.imageRect.extent = {s.panel.dx, s.panel.dy};
        lq.pose.orientation = {q.x, q.y, q.z, q.w};
        lq.pose.position = {pos.x, pos.y, pos.z};
        lq.size = {w, w * float(g_gl.renderHeight) / float(std::max(g_gl.renderWidth, 1))};
        // (the HUD over the per-eye world, stereo and first person: its premultiplied coverage, hud.cpp)
        if (!fScreenLook) lq.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        alayer.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&lq));
    }
    // the wristwatch: posed in the right grip's own space (the runtime keeps it on the controller), over the rest
    XrCompositionLayerQuad lw{XR_TYPE_COMPOSITION_LAYER_QUAD};
    if (s.fShould && fWatchFrame && s.watch.h && s.aspaceGrip[1] && lp.views && !s.fPanelScreen)
    {
        const glm::quat q = QuatWatchGrip();
        lw.space = s.aspaceGrip[1];
        lw.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        lw.subImage.swapchain = s.watch.h;
        lw.subImage.imageRect.offset = {0, 0};
        lw.subImage.imageRect.extent = {s.watch.dx, s.watch.dy};
        lw.pose.orientation = {q.x, q.y, q.z, q.w};
        lw.pose.position = {kposWatchGrip.x, kposWatchGrip.y, kposWatchGrip.z};
        lw.size = {kdxWatch, kdyWatch};
        lw.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;   // (premultiplied coverage, as the HUD's)
        alayer.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&lw));
    }
    XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
    ei.displayTime = s.fs.predictedDisplayTime;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    ei.layerCount = uint32_t(alayer.size());
    ei.layers = alayer.data();
    FCheck(xrEndFrame(s.sess, &ei), "xrEndFrame");
}

void ReadControllers(uint16_t* pgrf, float* pxl, float* pyl, float* pxr, float* pyr, bool fMenu)
{
    using namespace vrprofile;
    const PROFILE& pr = Prof();
    if (!s.sess || !s.fActions || !s.fRunning) return;
    XrActiveActionSet aas{s.actionset, XR_NULL_PATH};
    XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};
    si.countActiveActionSets = 1;
    si.activeActionSets = &aas;
    if (xrSyncActions(s.sess, &si) != XR_SUCCESS) return;   // (XR_SESSION_NOT_FOCUSED: a system menu has the hands)
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
    bool af[Ctl_Max]{};
    for (int i = 0; i < Ctl_Max; ++i)
    {
        gi.action = s.aactCtl[i];
        XrActionStateBoolean st{XR_TYPE_ACTION_STATE_BOOLEAN};
        af[i] = XR_SUCCEEDED(xrGetActionStateBoolean(s.sess, &gi, &st)) && st.isActive && st.currentState;
    }
    const double t = TNow();
    auto edge = [&](int i) { return af[i] && !s.afCtlPrev[i]; };
    // (0.1.2) the grips held (a grab ends when let go), both hands' velocities (FGripVelocityLast, the swing)
    s.afGripHeld[0] = af[Ctl_GripL];
    s.afGripHeld[1] = af[Ctl_GripR];
    for (int k = 0; k < 2; ++k)
        if (!s.afGripHeld[k]) s.afGripGrab[k] = false;
    ReadGripVelocities();
    // (the T-pose calibration has the controllers: both triggers held in the pose, B puts it off; nothing reaches the
    // game meanwhile)
    if (s.tposeStep == 2 && t - s.tTPoseDone > 2.5) s.tposeStep = 0;
    if (s.tposeStep == 1)
    {
        if (edge(Ctl_B))
        {
            s.tposeStep = 0;
            s.fTPoseWanted = false;
            std::printf("vr: T-pose put off (Options > VR Settings > Recalibrate (T-pose) asks again)\n");
        }
        else StepTPose(af, t);
        // (out of the pose, either way: whatever is held then, the triggers, the grips, B, stays out of the game until
        // let go)
        if (s.tposeStep != 1)
            for (int i = 0; i < Ctl_Max; ++i)
                if (af[i] && i != Ctl_StickClickL) s.afCtlChord[i] = true;   // (the stick click: no edge, no chord)
        std::copy(std::begin(af), std::end(af), s.afCtlPrev);
        s.fSprint = false;
        DropInteractions();
        return;
    }
    // (0.1.2) the physical interactions (vr.h): only first person at the player's head, with no menu up
    const bool fPhys = FFirstPersonEyes() && !fMenu;
    if (!fPhys) DropInteractions();
    StepTorso(t);   // (FHolsterWorld follows it in a menu too)
    // the holster behind the right shoulder (Sly): a tick on reaching it; the right grip's press there draws or
    // holsters the cane, and presses nothing else until let go
    {
        const bool fWas = s.fAtHolster;
        s.fAtHolster = fPhys && s.charCur == Char::Sly && FRightHandAtHolster();
        if (s.fAtHolster && !fWas) Pulse(1, 0.25f, 0.02f);
        if (s.fAtHolster && edge(Ctl_GripR))
        {
            s.fCaneDrawn = !s.fCaneDrawn;
            Pulse(1, 0.7f, 0.06f);
            s.afCtlChord[Ctl_GripR] = true;
        }
    }
    // the wristwatch (gadget_menu = watch, its picture made): open while the inside of the right wrist faces you; the
    // left trigger picks
    const bool fWatchMenu = pr.nGadgetMenu == 0 && s.watch.h;
    StepWatch(fPhys && fWatchMenu && !s.fAtHolster && !s.fHandCal, edge(Ctl_TriggerL), t);
    // the grips: a new press is offered to vrinteract (FTakeGripPress). One that begins at something to grab
    // (SetGrabZone, the step before) is a grab: its button (R1 on the right) kept from the game for the whole hold. Any
    // other presses its button as before, kcstepGripOffer steps late: until then vrinteract may still take it as a grab
    // (a hand reaching a pole as it squeezes), and a taken press never reached the game (no R1 for a step: no paraglider
    // opened and closed in the air). A squeeze while the sprint toggle holds R1 ends the toggle, so the grip's R1 is a
    // new press (running off a roof, then the right grip: the paraglider)
    bool afGripDefer[2]{};
    for (int k = 0; k < 2; ++k)
    {
        const int ctl = k ? Ctl_GripR : Ctl_GripL;
        if (s.acstepGripPending[k] > 0 && --s.acstepGripPending[k] == 0) s.afGripPending[k] = false;
        if (s.afGripPending[k] && !s.afGripGrab[k]) afGripDefer[k] = true;
        if (!fPhys || !edge(ctl) || (k == 1 && s.fAtHolster)) continue;
        if (s.fSprint && (pr.abtnCtl[ctl] & BTN_R1)) s.fSprint = false, s.tSprintRest = -1.0;
        s.afGripPending[k] = true;
        if (s.afGrabZone[k]) s.acstepGripPending[k] = kcstepGripPending, s.afCtlChord[ctl] = true, s.afGripGrab[k] = true;
        else s.acstepGripPending[k] = kcstepGripOffer, afGripDefer[k] = true;
    }
    // attacks, first person: Sly's cane holstered: no swing, no Square from the right trigger, no Triangle from X; a
    // pocket near, the right hand at something to grab or the watch open: no swing (anyone), nor Sly's right-trigger
    // attack; a grab held with either hand: no swing (other characters' triggers are theirs: Bentley's darts)
    uint16_t agrfOff[Ctl_Max]{};
    bool fNoSwing = false;
    if (fPhys)
    {
        const bool fSly = s.charCur == Char::Sly;
        const bool fBusy = s.fPocketNear || s.afGrabZone[1] || s.fWatchOpen;
        if (fSly && !s.fCaneDrawn)
        {
            agrfOff[Ctl_TriggerR] |= BTN_Square;
            agrfOff[Ctl_X] |= BTN_Triangle;
            fNoSwing = true;
        }
        if (fSly && fBusy) agrfOff[Ctl_TriggerR] |= kgrfAttack;
        if (fBusy || s.afGrabZone[0] || s.afGripGrab[0] || s.afGripGrab[1]) fNoSwing = true;
    }
    uint16_t grfVr = 0;
    auto press = [&](uint32_t btn) { grfVr |= uint16_t(btn & 0xFFFF); };
    auto pressCtl = [&](int i, uint32_t btn) { press(btn & ~uint32_t(agrfOff[i])); };   // (less what is off for it)
    // a tap's action: the sprint toggle at once, a button for a few steps (long enough for the game to see it)
    auto tap = [&](int i) {
        if (pr.abtnCtl[i] == kBtnSprintToggle)
        {
            s.fSprint = !s.fSprint;
            s.tSprintRest = -1.0;
        }
        else s.acstepTap[i] = 3;
    };
    // the left stick click's chord: + A reload, + B recentre, + X hand calibration, + Y height calibration; alone: a tap
    // (under half a second) is its own action, a longer press its _hold action while held
    if (edge(Ctl_StickClickL))
    {
        s.fChord = true;
        s.fChordUsed = false;
        s.tChordDown = t;
        s.afHoldFired[Ctl_StickClickL] = false;
    }
    if (s.fChord)
    {
        const struct { int ctl; Helper h; } achord[] = {{Ctl_A, Helper::Reload}, {Ctl_B, Helper::Recenter},
                                                        {Ctl_X, Helper::HandCal}, {Ctl_Y, Helper::HeightCal}};
        for (const auto& c : achord)
            if (edge(c.ctl))
            {
                DoHelper(c.h);
                s.fChordUsed = true;
                s.afCtlChord[c.ctl] = true;
            }
        if (af[Ctl_StickClickL])
        {
            if (!s.fChordUsed && pr.abtnHold[Ctl_StickClickL] && t - s.tChordDown >= kDtHold) s.afHoldFired[Ctl_StickClickL] = true;
            if (s.afHoldFired[Ctl_StickClickL]) press(pr.abtnHold[Ctl_StickClickL]);
        }
        else
        {
            // (with a long press set: any release before it fired is a tap; without one, a press under kDtHold)
            if (!s.fChordUsed && !s.afHoldFired[Ctl_StickClickL] && (pr.abtnHold[Ctl_StickClickL] || t - s.tChordDown < kDtHold))
                tap(Ctl_StickClickL);
            s.fChord = false;
        }
    }
    for (int i = 0; i < Ctl_Max; ++i)
    {
        if (i != Ctl_StickClickL)
        {
            // (a button that started a helper stays the chord's until released, the release's frame too: no tap of it
            // follows; one held before the chord began is not pressed again by it)
            const bool fChordKey = s.afCtlChord[i] || (s.fChord && edge(i) && (i == Ctl_A || i == Ctl_B || i == Ctl_X || i == Ctl_Y));
            if (!af[i]) s.afCtlChord[i] = false;
            // (a grip's new press not at something to grab: its button from the next read)
            const bool fDefer = (i == Ctl_GripL && afGripDefer[0]) || (i == Ctl_GripR && afGripDefer[1]);
            if (fChordKey) {}
            else if (!pr.abtnHold[i])
            {
                // no long press: at once (the sprint toggle on the press)
                if (pr.abtnCtl[i] == kBtnSprintToggle) { if (edge(i)) tap(i); }
                else if (af[i] && !fDefer) pressCtl(i, pr.abtnCtl[i]);
            }
            else
            {
                // a tap acts when let go, a hold once it has lasted half a second (while held)
                if (edge(i)) s.atDown[i] = t, s.afHoldFired[i] = false;
                if (af[i])
                {
                    if (!s.afHoldFired[i] && t - s.atDown[i] >= kDtHold) s.afHoldFired[i] = true;
                    if (s.afHoldFired[i]) pressCtl(i, pr.abtnHold[i]);
                }
                else if (s.afCtlPrev[i] && !s.afHoldFired[i]) tap(i);
            }
        }
        if (s.acstepTap[i] > 0)
        {
            --s.acstepTap[i];
            pressCtl(i, pr.abtnCtl[i]);
        }
    }
    for (int i : {int(Ctl_A), int(Ctl_B), int(Ctl_TriggerR), int(Ctl_GripR), int(Ctl_StickClickR), int(Ctl_PadR)})
        if (af[i]) s.tRightActive = t;
    for (int i = 0; i < Ctl_Max; ++i)
        if (edge(i)) s.fPromptVr = true;   // (the prompts follow: the VR controllers' icons)
    std::copy(std::begin(af), std::end(af), s.afCtlPrev);
    // (gadget_menu = watch, first person at the head: Y's Select opens no gadget menu, the watch is the picker)
    if (fWatchMenu && FFirstPersonEyes()) grfVr &= uint16_t(~BTN_Select);
    // vrinteract's buttons (RequestButtons: a tap, or held while asked each step)
    for (int b = 0; b < 16; ++b)
        if (s.acstepReq[b] > 0)
        {
            --s.acstepReq[b];
            grfVr |= uint16_t(1u << b);
        }

    auto stick = [&](XrAction act, float* px, float* py) {
        gi.action = act;
        XrActionStateVector2f st{XR_TYPE_ACTION_STATE_VECTOR2F};
        if (XR_FAILED(xrGetActionStateVector2f(s.sess, &gi, &st)) || !st.isActive) return glm::vec2(0.0f);
        const glm::vec2 v(st.currentState.x, st.currentState.y);   // (y up, as the pad's)
        if (px && py && glm::length(v) > std::sqrt(*px * *px + *py * *py)) { *px = v.x; *py = v.y; }
        return v;
    };
    const glm::vec2 move = stick(s.actMove, pxl, pyl);
    if (glm::length(move) > 0.5f) s.fPromptVr = true;
    // (vrinteract's climbing hand over hand: the left stick is its own while set, y > 0 up as the pad's; set again each
    // step, else it lets go)
    if (s.fClimbStick)
    {
        if (++s.cstepClimbStick > kcstepClimbStale) s.fClimbStick = false;
        else *pxl = s.vClimbStick.x, *pyl = s.vClimbStick.y;
    }
    // the sprint toggle: R1 kept on until the stick rests a moment, a jump (R1 in the air: the paraglider), or a menu
    if (s.fSprint)
    {
        if (glm::length(move) < 0.2f)
        {
            if (s.tSprintRest < 0.0) s.tSprintRest = t;
            else if (t - s.tSprintRest > 0.3) s.fSprint = false;
        }
        else s.tSprintRest = -1.0;
        // (a jump, a menu, or Select: the gadget menu opens only with R1 up)
        if (((grfVr & ~s.grfVrPrev) & (BTN_Cross | BTN_Select)) || fMenu) s.fSprint = false;
        if (s.fSprint) grfVr |= BTN_R1;
    }
    *pgrf |= grfVr;
    s.grfVrPrev = grfVr;
    if (!FFirstPersonEyes())   // (stereo, or first person's game-camera view in a scene: the right stick is the game's)
    {
        if (glm::length(stick(s.actTurn, pxr, pyr)) > 0.3f) s.tRightActive = t;
        return;
    }
    // first person: the right stick's x snap-turns 30 degrees (to the right: the heading decreases); its y still
    // reaches the game (the binocucom's zoom, the hacking ship's guns, the RC tank's right track)
    const glm::vec2 turn = stick(s.actTurn, nullptr, nullptr);
    const bool fSnap = std::fabs(turn.x) > 0.7f;
    if (fSnap && !s.fSnapWas && !fMenu) s.yawBody += turn.x > 0.0f ? -glm::radians(30.0f) : glm::radians(30.0f);
    s.fSnapWas = std::fabs(turn.x) > 0.4f;   // (released below 0.4: a little hysteresis)
    if (std::fabs(turn.y) > std::fabs(*pyr)) *pyr = turn.y;
    if (glm::length(turn) > 0.3f) s.tRightActive = t;
    // a swing: the right hand faster than swing_speed presses swing's button (swing_up's when it goes mostly up) for a
    // few steps, then rests (one attack a swing); none while the hands are calibrated, nor while attacks are off above
    if (s.cstepSwingCool > 0) --s.cstepSwingCool;
    if (fMenu)
    {
        // (a menu has the pad: a hand's flick must not answer it; a swing in progress is dropped)
        s.cstepSwingHold = 0;
        s.cstepSwingDecide = 0;
        return;
    }
    if (s.cstepSwingHold > 0)
    {
        --s.cstepSwingHold;
        *pgrf |= s.btnSwingHeld;
    }
    else if (!s.fHandCal && s.tposeStep == 0 && s.cstepSwingCool == 0 && !fNoSwing)
    {
        if (s.afVelGrip[1])   // (ReadGripVelocities: LOCAL, m/s)
        {
            const glm::vec3 v = s.avelGripLocal[1];
            const float speed = glm::length(v);
            if (s.cstepSwingDecide == 0 && speed > pr.mpsSwing)
            {
                s.cstepSwingDecide = 5;   // (a swing begun: watch it for 5 steps, about 80 ms)
                s.vyPeak = 0.0f;
                s.speedPeak = 0.0f;
            }
            if (s.cstepSwingDecide > 0)
            {
                s.vyPeak = std::max(s.vyPeak, v.y);   // (LOCAL space: Y up)
                s.speedPeak = std::max(s.speedPeak, speed);
                if (--s.cstepSwingDecide == 0)
                {
                    // (an upward swing: its strongest upward push is at least 0.45 of its strongest speed and above
                    // the swing speed's half; a sideways or forward swing keeps well below it)
                    const bool fUp = s.vyPeak > 0.55f * s.speedPeak && s.vyPeak > 0.5f * pr.mpsSwing;
                    const uint16_t btn = fUp && pr.btnSwingUp ? pr.btnSwingUp : pr.btnSwing;
                    if (btn)
                    {
                        s.cstepSwingHold = 3;    // (3 steps, 1/20 s: long enough for the game to see the press)
                        s.cstepSwingCool = 21;   // (about a third of a second before the next swing counts)
                        s.btnSwingHeld = btn;
                        *pgrf |= btn;
                    }
                }
                s.tRightActive = t;
            }
        }
    }
}

bool FFirstPersonPan(float* pradPan)
{
    if (!FFirstPersonEyes() || !s.sess || !s.fBodyInit || !s.fViews) return false;
    *pradPan = s.yawBody + RadHeadYaw();
    return true;
}

float RadBodyYaw(float radCamPan)
{
    if (!s.fBodyInit)
    {
        s.yawBody = radCamPan;
        s.fBodyInit = true;
    }
    return s.yawBody;
}

float SEyeHeight()
{
    // (sly_size: the eye above the character's floor grows with you, as the scale does: your real floor stays the
    // virtual one)
    return s.sEyeAboveFloor > 0.0f ? Prof().sEye + (SlySize() - 1.0f) * s.sEyeAboveFloor : Prof().sEye;
}

bool FHeadView(glm::vec3* ppos, glm::mat3* pmatRows, float* ptanX, float* ptanY, uint64_t* pserial)
{
    if (!s.sess || !s.fHead || !FEyes()) return false;
    *ppos = s.posHead;
    *pmatRows = s.matHeadRows;
    *ptanX = s.tanHeadX;
    *ptanY = s.tanHeadY;
    *pserial = s.serialHead;
    return true;
}

bool FHandWorld(int k, const CAMERA& cam, glm::mat4* pmat)
{
    if (k < 0 || k > 1 || !s.afGrip[k]) return false;
    const glm::mat4 m = MatCamOf(cam) * MatTrackedLocal(s.aposeGrip[k]);
    // (the position in the world, the turn without the camera's scale: the cane keeps its own size)
    glm::mat4 r(1.0f);
    r[0] = glm::vec4(glm::normalize(glm::vec3(m[0])), 0.0f);
    r[1] = glm::vec4(glm::normalize(glm::vec3(m[1])), 0.0f);
    r[2] = glm::vec4(glm::normalize(glm::vec3(m[2])), 0.0f);
    r[3] = m[3];
    // (0.1.3) the player's own tuning of the bare hands (hand_turn / hand_offset, VR Settings), in the grip's axes; the
    // left hand's mirror image across the grip's X, as the calibration's
    const vrprofile::PROFILE& pr = Prof();
    glm::vec3 deg = pr.degHand, cm = pr.cmHand;
    if (k == 0)
    {
        deg = glm::vec3(deg.x, -deg.y, -deg.z);
        cm.x = -cm.x;
    }
    const glm::mat4 matTune = glm::translate(glm::mat4(1.0f), cm * (s.scale / 100.0f)) * glm::mat4_cast(glm::quat(glm::radians(deg)));
    *pmat = r * matTune * MatHandCal(k);
    return true;
}

bool FAimRay(int k, glm::vec3* ppos, glm::vec3* pdir)
{
    if (!s.sess || !FEyes() || k < 0 || k > 1 || !s.afAim[k] || !s.fCamLast) return false;
    // (a right controller put down while a gamepad plays does not take the aim: used within the last 15 s)
    if (k == 1 && TNow() - s.tRightActive > 15.0) return false;
    const glm::mat4 m = s.matCamLast * MatTrackedLocal(s.aposeAim[k]);
    *ppos = glm::vec3(m[3]);
    *pdir = glm::normalize(-glm::vec3(m[2]));   // (the aim pose points along its -Z)
    return true;
}

// ---- (0.1.2) the physical interactions (vr.h)
void Pulse(int k, float amplitude, float seconds, float frequency)
{
    // (nothing without the motion controllers or with the launcher's Controller vibration off; dropped while the
    // session is unfocused: xrApplyHapticFeedback answers XR_SESSION_NOT_FOCUSED, a success)
    if (!s.sess || !s.fActions || !s.fRunning || !s.actHaptic || k < 0 || k > 1 || !g_fControllerVibration) return;
    XrHapticActionInfo hai{XR_TYPE_HAPTIC_ACTION_INFO};
    hai.action = s.actHaptic;
    hai.subactionPath = s.apathHand[k];   // (else both hands)
    static bool s_fLogged = false;        // (a failure logged once: the pulses come often)
    if (amplitude <= 0.0f)
    {
        const XrResult r = xrStopHapticFeedback(s.sess, &hai);
        if (XR_FAILED(r) && !std::exchange(s_fLogged, true)) FCheck(r, "xrStopHapticFeedback");
        return;
    }
    XrHapticVibration vib{XR_TYPE_HAPTIC_VIBRATION};
    vib.amplitude = std::min(amplitude, 1.0f);
    vib.duration = seconds > 0.0f ? XrDuration(double(seconds) * 1e9) : XR_MIN_HAPTIC_DURATION;   // (nanoseconds)
    vib.frequency = frequency > 0.0f ? frequency : XR_FREQUENCY_UNSPECIFIED;
    const XrResult r = xrApplyHapticFeedback(s.sess, &hai, reinterpret_cast<const XrHapticBaseHeader*>(&vib));
    if (XR_FAILED(r) && !std::exchange(s_fLogged, true)) FCheck(r, "xrApplyHapticFeedback");
}

bool FCaneDrawn() { return s.fCaneDrawn; }
void SetCaneDrawn(bool f) { s.fCaneDrawn = f; }

bool FGripWorld(int k, const CAMERA& cam, glm::mat4* pmat)
{
    if (k < 0 || k > 1 || !s.afGrip[k] || !FFirstPersonEyes()) return false;
    *pmat = MatCamOf(cam) * MatTrackedLocal(s.aposeGrip[k]);   // (MatCamOf and the grip's turn are orthonormal: no scale)
    return true;
}

bool FGripWorldLast(int k, glm::mat4* pmat)
{
    if (k < 0 || k > 1 || !s.afGrip[k] || !s.fCamLast || !FFirstPersonEyes()) return false;
    *pmat = s.matCamLast * MatTrackedLocal(s.aposeGrip[k]);
    return true;
}

bool FGripVelocityLast(int k, glm::vec3* pvelWorld, glm::vec3* pvelLocal)
{
    if (k < 0 || k > 1 || !s.afVelGrip[k] || !s.fCamLast || !FFirstPersonEyes()) return false;
    const glm::quat qUnyaw = glm::angleAxis(-s.yawOrigin, glm::vec3(0.0f, 1.0f, 0.0f));
    if (pvelLocal) *pvelLocal = s.avelGripLocal[k];
    // (as the positions: the recentre's turn, the scale, the camera's turn; no translation)
    if (pvelWorld) *pvelWorld = glm::mat3(s.matCamLast) * (qUnyaw * s.avelGripLocal[k]) * s.scale;
    return true;
}

bool FHolsterWorld(const CAMERA& cam, glm::mat4* pmat)
{
    glm::vec3 pos(0.0f);
    glm::mat3 mat(1.0f);
    if (!s.sess || !FFirstPersonEyes() || !FHolsterLocal(&pos, &mat)) return false;
    *pmat = MatWorldOfLocal(MatCamOf(cam), pos, mat);
    return true;
}

float UnitsPerMetre() { return s.scale; }

bool FGripHeld(int k) { return k >= 0 && k <= 1 && s.sess && s.fActions && s.afGripHeld[k]; }

bool FTakeGripPress(int k)
{
    if (k < 0 || k > 1 || !s.afGripPending[k] || !FFirstPersonEyes()) return false;
    s.afGripPending[k] = false;
    s.acstepGripPending[k] = 0;
    // (taken: a grab, its button (R1 on the right) kept from the game until let go)
    if (s.afGripHeld[k]) s.afCtlChord[k ? vrprofile::Ctl_GripR : vrprofile::Ctl_GripL] = true, s.afGripGrab[k] = true;
    return true;
}

void SetGrabZone(int k, bool f)
{
    if (k < 0 || k > 1) return;
    if (f && !s.afGrabZone[k] && FFirstPersonEyes()) Pulse(k, 0.2f, 0.02f);   // (a tick on reaching it)
    s.afGrabZone[k] = f;
}

void SetPocketNear(bool f) { s.fPocketNear = f; }

void RequestButtons(uint16_t grf, int csteps)
{
    for (int b = 0; b < 16; ++b)
        if (grf & (1u << b)) s.acstepReq[b] = std::max(s.acstepReq[b], csteps);
}

void SetClimbStick(bool fSet, float x, float y)
{
    s.fClimbStick = fSet;
    s.vClimbStick = glm::clamp(glm::vec2(x, y), glm::vec2(-1.0f), glm::vec2(1.0f));
    s.cstepClimbStick = 0;
}

bool FGlideLines(float* pturn, float* ppull)
{
    if (!FFirstPersonEyes() || !Prof().fGlideLines || !s.fViews || !s.afGrip[0] || !s.afGrip[1] || !FGripHeld(0) ||
        !FGripHeld(1))
        return false;
    // (LOCAL: Y up, metres) both hands raised (above your head less 0.45 m), as on a paraglider's lines: the left
    // pulled lower turns left (negative), both pulled down brake
    const float yHead = PosHeadLocal().y;
    const float yL = s.aposeGrip[0].position.y, yR = s.aposeGrip[1].position.y;
    if (yL < yHead - kmGlideBelowHead || yR < yHead - kmGlideBelowHead) return false;
    if (pturn) *pturn = std::clamp((yL - yR) / 0.25f, -1.0f, 1.0f);
    if (ppull) *ppull = std::clamp((yHead + 0.05f - 0.5f * (yL + yR)) / 0.4f, 0.0f, 1.0f);
    return true;
}

bool FWatchOpen() { return s.sess && s.fWatchOpen && FFirstPersonEyes() && Prof().nGadgetMenu == 0 && s.watch.h; }

void SetWatchEntries(const std::vector<std::string>& astr, int iCur)
{
    if (astr != s.astrWatch) s.astrWatch = astr;
    s.iWatchCur = iCur;
}

bool FWatchState(std::vector<std::string>* pastr, int* piCur, int* piHover)
{
    if (!FWatchOpen()) return false;
    if (pastr) *pastr = s.astrWatch;
    if (piCur) *piCur = s.iWatchCur;
    if (piHover) *piHover = s.iWatchHover < int(s.astrWatch.size()) ? s.iWatchHover : -1;
    return true;
}

int TakeWatchPick() { return std::exchange(s.iWatchPick, -1); }

void SetCharacter(vrprofile::Char c, float sCenterAboveFloor, bool fCanCalibrate)
{
    s.charCur = c;
    s.fCanCalNow = fCanCalibrate;
    // the height calibration: your eye height maps to the character's (its eye above its floor), else the profile's;
    // either times sly_size (a bigger you: more units per metre, the eye higher by as much: SEyeHeight)
    const float mPlayer = vrprofile::MPlayerEye();
    s.sEyeAboveFloor = sCenterAboveFloor > 0.0f ? sCenterAboveFloor + Prof().sEye : 0.0f;
    s.scale = mPlayer > 0.0f && s.sEyeAboveFloor > 1.0f ? std::clamp(s.sEyeAboveFloor / mPlayer, 10.0f, 1000.0f) : Prof().scale;
    s.scale *= SlySize();
}
vrprofile::Char CharacterCur() { return s.charCur; }
const vrprofile::PROFILE& ProfileCur() { return Prof(); }
bool FHandCalibrating() { return s.sess && s.fHandCal && FFirstPerson(); }

void SetFirstPersonEyes(bool f)
{
    if (f && !s.fFpEyes) s.fBodyInit = false;   // (back from a scene: facing where the game camera faces)
    s.fFpEyes = f;
}
bool FFirstPersonEyes() { return s.sess && s.mode == Mode::FirstPerson && s.fFpEyes; }
void BlinkView() { s.tBlink = TNow(); }
bool FGuidedMessage(std::vector<std::string>* palines, float* puProgress)
{
    *puProgress = -1.0f;
    if (!s.sess || !s.tposeStep) return false;
    if (s.tposeStep == 2)
    {
        *palines = {"All set!", "Recalibrate any time: Options > VR Settings"};
        return true;
    }
    const char* szState = s.szTPoseHint ? s.szTPoseHint : s.tTPoseHold >= 0.0 ? "Hold it..." : " ";
    *palines = {"VR setup", "Stand up straight and face forward.", "Hold your arms straight out to your sides,",
                "then hold BOTH TRIGGERS for 2 seconds.", szState, "B: later"};
    *puProgress = s.tTPoseHold >= 0.0 ? float(std::clamp((TNow() - s.tTPoseHold) / kDtTPose, 0.0, 1.0)) : 0.0f;
    return true;
}
void NoteTitle(bool fTitle)
{
    if (!s.sess || s.mode != Mode::FirstPerson) return;
    // (asked for, but the right controller (B's: the only way to put it off) untracked for 3 s, e.g. a gamepad game
    // with the controllers asleep, the system menu: put off until it is back, as nothing else can answer it)
    if (s.tposeStep == 1)
    {
        const double t = TNow();
        if (s.afGrip[1]) s.tTPoseGrip = t;
        else if (t - s.tTPoseGrip > 3.0)
        {
            s.tposeStep = 0;
            s.fTPoseAsked = false;
            // (what was held at the last read stays out of the game until let go)
            for (int i = 0; i < vrprofile::Ctl_Max; ++i)
                if (s.afCtlPrev[i] && i != vrprofile::Ctl_StickClickL) s.afCtlChord[i] = true;
            std::printf("vr: T-pose put off: the right controller is not tracked (asked again when it is)\n");
        }
        return;
    }
    if (s.tposeStep || s.fTPoseAsked) return;
    if (!fTitle && !s.fCanCalNow) return;
    if (!s.fActions || !s.afGrip[1]) return;   // (it needs the motion controllers: the right one carries B)
    if (!s.fCalForced && !s.fTPoseWanted && vrprofile::FTPose(nullptr, nullptr))
    {
        s.fTPoseAsked = true;   // (done before: not asked again)
        return;
    }
    StartTPose();
}
void RequestTPose()
{
    if (!s.sess || s.mode != Mode::FirstPerson || !s.fActions) return;
    s.fTPoseWanted = true;
    StartTPose();
}
void SetCharacterTPose(const glm::mat3* pamat2)
{
    s.fCharT = pamat2 != nullptr;
    if (pamat2) s.amatCharT[0] = pamat2[0], s.amatCharT[1] = pamat2[1];
}
void OfferHandPose(int k, const glm::mat4& matHandWorld)
{
    if (k < 0 || k > 1) return;
    s.amatHandAnim[k] = matHandWorld;
    s.afHandAnim[k] = true;
}
uint16_t GrfAutoPause(bool fMenuUp)
{
    if (s.cstepPause > 0)
    {
        --s.cstepPause;
        return BTN_Start;
    }
    if (!s.fPauseWanted) return 0;
    s.fPauseWanted = false;
    static const bool s_fOff = FloatEnv("SLY2N_VR_AUTOPAUSE", 1.0f) == 0.0f;
    if (fMenuUp || s_fOff || !s.sess) return 0;
    std::printf("vr: paused (the headset lost focus: taken off, or the system menu)\n");
    s.cstepPause = 2;   // (Start held three steps: the pause menu opens on the press)
    return BTN_Start;
}
bool FIndexControllers() { return s.fIndex; }
bool FPromptsVr() { return s.sess && s.fActions && s.fPromptVr; }
void NoteOtherInput() { s.fPromptVr = false; }
int CtlForButton(uint16_t btn)
{
    if (!btn) return -1;
    // (the right trackpad is the Index's only: on the Touch, the button's other control)
    auto fHas = [](int i) { return i != vrprofile::Ctl_PadR || s.fIndex; };
    for (int i = 0; i < vrprofile::Ctl_Max; ++i)
        if (Prof().abtnCtl[i] == btn && fHas(i)) return i;
    for (int i = 0; i < vrprofile::Ctl_Max; ++i)   // (else a long press: its control's icon)
        if (Prof().abtnHold[i] == btn && fHas(i)) return i;
    return -1;
}

void NoteAimUsed(const glm::vec3& posFrom, const glm::vec3& posTo)
{
    s.posPointerFrom = posFrom;
    s.posPointerTo = posTo;
    s.fPointerLocal = false;
    s.cframePointer = 20;   // (displayed frames: about a fifth of a second after the last aim)
}

namespace
{
bool FPointerProgram()
{
    if (s.progPointer) return true;
    const char* szVs = "#version 430 core\nlayout(location = 0) in vec4 aPos;\nvoid main() { gl_Position = aPos; }\n";
    const char* szFs = "#version 430 core\nuniform vec4 uColor;\nout vec4 oColor;\nvoid main() { oColor = uColor; }\n";
    auto compile = [](GLenum type, const char* sz) {
        const GLuint sh = glCreateShader(type);
        glShaderSource(sh, 1, &sz, nullptr);
        glCompileShader(sh);
        GLint f = 0;
        glGetShaderiv(sh, GL_COMPILE_STATUS, &f);
        if (!f) { glDeleteShader(sh); return GLuint(0); }
        return sh;
    };
    const GLuint vs = compile(GL_VERTEX_SHADER, szVs), fs = compile(GL_FRAGMENT_SHADER, szFs);
    if (!vs || !fs)
    {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        std::fprintf(stderr, "vr: the pointer's shaders did not compile\n");
        return false;
    }
    s.progPointer = glCreateProgram();
    glAttachShader(s.progPointer, vs);
    glAttachShader(s.progPointer, fs);
    glLinkProgram(s.progPointer);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint f = 0;
    glGetProgramiv(s.progPointer, GL_LINK_STATUS, &f);
    if (!f)
    {
        glDeleteProgram(s.progPointer);
        s.progPointer = 0;
        std::fprintf(stderr, "vr: the pointer's program did not link\n");
        return false;
    }
    s.iuColor = glGetUniformLocation(s.progPointer, "uColor");
    glGenVertexArrays(1, &s.vaoPointer);
    glGenBuffers(1, &s.vboPointer);
    glBindVertexArray(s.vaoPointer);
    glBindBuffer(GL_ARRAY_BUFFER, s.vboPointer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec4) * 12, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(glm::vec4), nullptr);
    glBindVertexArray(0);
    return true;
}
}  // namespace

void DrawAimPointer(const VIEWOVERRIDE& eye)
{
    if (s.cframePointer <= 0 || !s.sess) return;
    if (s.fPointerLocal && !s.fCamLast) return;
    const glm::vec3 posFrom = s.fPointerLocal ? PosWorldOfLocal(s.matCamLast, s.posPtrFromLocal) : s.posPointerFrom;
    const glm::vec3 posTo = s.fPointerLocal ? PosWorldOfLocal(s.matCamLast, s.posPtrToLocal) : s.posPointerTo;
    // the segment in clip space, cut where it passes behind the eye
    glm::vec4 c0 = eye.matWorldToClip * glm::vec4(posFrom, 1.0f);
    glm::vec4 c1 = eye.matWorldToClip * glm::vec4(posTo, 1.0f);
    const float wMin = 1e-3f;
    if (c0.w < wMin && c1.w < wMin) return;
    if (c0.w < wMin) c0 = glm::mix(c0, c1, (wMin - c0.w) / (c1.w - c0.w));
    if (c1.w < wMin) c1 = glm::mix(c1, c0, (wMin - c1.w) / (c0.w - c1.w));
    // a band a few pixels wide (core GL draws one-pixel lines only), and a square at the end
    const glm::vec2 size(float(g_gl.renderWidth), float(g_gl.renderHeight));
    const glm::vec2 n0 = glm::vec2(c0) / c0.w, n1 = glm::vec2(c1) / c1.w;
    glm::vec2 d = (n1 - n0) * size;
    if (glm::length(d) < 1e-3f) d = glm::vec2(1.0f, 0.0f);
    const glm::vec2 perp = glm::normalize(glm::vec2(-d.y, d.x)) * 2.0f / size;   // (2 px each side, in NDC: /size*2)
    const glm::vec2 hDot = 2.0f * 6.0f / size;                                     // (6 px each side)
    const glm::vec4 av[12] = {
        glm::vec4(glm::vec2(c0) + perp * c0.w, c0.z, c0.w), glm::vec4(glm::vec2(c0) - perp * c0.w, c0.z, c0.w),
        glm::vec4(glm::vec2(c1) + perp * c1.w, c1.z, c1.w),
        glm::vec4(glm::vec2(c1) + perp * c1.w, c1.z, c1.w), glm::vec4(glm::vec2(c0) - perp * c0.w, c0.z, c0.w),
        glm::vec4(glm::vec2(c1) - perp * c1.w, c1.z, c1.w),
        glm::vec4(glm::vec2(c1) + glm::vec2(-hDot.x, -hDot.y) * c1.w, c1.z, c1.w), glm::vec4(glm::vec2(c1) + glm::vec2(hDot.x, -hDot.y) * c1.w, c1.z, c1.w),
        glm::vec4(glm::vec2(c1) + glm::vec2(hDot.x, hDot.y) * c1.w, c1.z, c1.w),
        glm::vec4(glm::vec2(c1) + glm::vec2(-hDot.x, -hDot.y) * c1.w, c1.z, c1.w), glm::vec4(glm::vec2(c1) + glm::vec2(hDot.x, hDot.y) * c1.w, c1.z, c1.w),
        glm::vec4(glm::vec2(c1) + glm::vec2(-hDot.x, hDot.y) * c1.w, c1.z, c1.w)};
    // (the renderer keeps a cache of the GL state: everything changed here is put back as it was)
    GLint prog = 0, vao = 0, vbo = 0, fbo = 0, aviewport[4]{}, depthFunc = 0, srcRgb = 0, dstRgb = 0, srcA = 0, dstA = 0, eqRgb = 0, eqA = 0;
    GLboolean fDepth = glIsEnabled(GL_DEPTH_TEST), fBlend = glIsEnabled(GL_BLEND), fCull = glIsEnabled(GL_CULL_FACE),
              fScissor = glIsEnabled(GL_SCISSOR_TEST), fStencil = glIsEnabled(GL_STENCIL_TEST), fDepthMask = GL_TRUE, amask[4]{};
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &vbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
    glGetIntegerv(GL_VIEWPORT, aviewport);
    glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
    glGetIntegerv(GL_BLEND_SRC_RGB, &srcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &dstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dstA);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &eqRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &eqA);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &fDepthMask);
    glGetBooleanv(GL_COLOR_WRITEMASK, amask);
    if (FPointerProgram())
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_gl.FboScene());
        glViewport(0, 0, g_gl.renderWidth, g_gl.renderHeight);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_GEQUAL);   // (reversed Z: greater is nearer; hidden behind the walls in front of it)
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glUseProgram(s.progPointer);
        glBindVertexArray(s.vaoPointer);
        glBindBuffer(GL_ARRAY_BUFFER, s.vboPointer);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(av), av);
        glUniform4f(s.iuColor, 0.45f, 0.85f, 1.0f, 0.55f);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glUniform4f(s.iuColor, 1.0f, 1.0f, 1.0f, 0.9f);
        glDrawArrays(GL_TRIANGLES, 6, 6);
    }
    glUseProgram(GLuint(prog));
    glBindVertexArray(GLuint(vao));
    glBindBuffer(GL_ARRAY_BUFFER, GLuint(vbo));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(fbo));
    glViewport(aviewport[0], aviewport[1], aviewport[2], aviewport[3]);
    glDepthFunc(GLenum(depthFunc));
    glDepthMask(fDepthMask);
    glBlendEquationSeparate(GLenum(eqRgb), GLenum(eqA));
    glBlendFuncSeparate(GLenum(srcRgb), GLenum(dstRgb), GLenum(srcA), GLenum(dstA));
    glColorMask(amask[0], amask[1], amask[2], amask[3]);
    auto set = [](GLenum cap, GLboolean f) { if (f) glEnable(cap); else glDisable(cap); };
    set(GL_DEPTH_TEST, fDepth);
    set(GL_BLEND, fBlend);
    set(GL_CULL_FACE, fCull);
    set(GL_SCISSOR_TEST, fScissor);
    set(GL_STENCIL_TEST, fStencil);
}

void DrawEyeFade()
{
    // black for a moment (the cut is only known after its step), then a 0.2 s fade in
    const double dt = TNow() - s.tBlink;
    const float a = dt < 0.04 ? 1.0f : float(1.0 - (dt - 0.04) / 0.2);
    if (!s.sess || a <= 0.0f || !FPointerProgram()) return;
    const glm::vec4 av[6] = {{-1, -1, 0, 1}, {1, -1, 0, 1}, {1, 1, 0, 1}, {-1, -1, 0, 1}, {1, 1, 0, 1}, {-1, 1, 0, 1}};
    GLint prog = 0, vao = 0, vbo = 0, fbo = 0, aviewport[4]{}, srcRgb = 0, dstRgb = 0, srcA = 0, dstA = 0;
    const GLboolean fDepth = glIsEnabled(GL_DEPTH_TEST), fBlend = glIsEnabled(GL_BLEND), fCull = glIsEnabled(GL_CULL_FACE),
                    fScissor = glIsEnabled(GL_SCISSOR_TEST), fStencil = glIsEnabled(GL_STENCIL_TEST);
    GLboolean amask[4]{};
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &vbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
    glGetIntegerv(GL_VIEWPORT, aviewport);
    glGetIntegerv(GL_BLEND_SRC_RGB, &srcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &dstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dstA);
    glGetBooleanv(GL_COLOR_WRITEMASK, amask);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_gl.FboScene());
    glViewport(0, 0, g_gl.renderWidth, g_gl.renderHeight);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    glUseProgram(s.progPointer);
    glBindVertexArray(s.vaoPointer);
    glBindBuffer(GL_ARRAY_BUFFER, s.vboPointer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(av), av);
    glUniform4f(s.iuColor, 0.0f, 0.0f, 0.0f, std::min(a, 1.0f));
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glUseProgram(GLuint(prog));
    glBindVertexArray(GLuint(vao));
    glBindBuffer(GL_ARRAY_BUFFER, GLuint(vbo));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(fbo));
    glViewport(aviewport[0], aviewport[1], aviewport[2], aviewport[3]);
    glBlendFuncSeparate(GLenum(srcRgb), GLenum(dstRgb), GLenum(srcA), GLenum(dstA));
    glColorMask(amask[0], amask[1], amask[2], amask[3]);
    auto set = [](GLenum cap, GLboolean f) { if (f) glEnable(cap); else glDisable(cap); };
    set(GL_DEPTH_TEST, fDepth);
    set(GL_BLEND, fBlend);
    set(GL_CULL_FACE, fCull);
    set(GL_SCISSOR_TEST, fScissor);
    set(GL_STENCIL_TEST, fStencil);
}
}  // namespace vr

#endif  // SLY2N_VR
