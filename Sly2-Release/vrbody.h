#pragma once

// (PC, VR first person: vr.h) The player drawn as its two hands only, on the motion controllers: no body tracking.
// The player's own skinned model is kept: its hand bones (and the fingers under them, and the forearm with
// SLY2N_VR_FOREARM=1, the default) are moved onto the controllers' grips. A skinned group with none of those bones is
// hidden outright; in a group that has some, every other bone is closed up at its arm's end (the forearm's or the
// hand's origin, on the controller), so a vertex shared with the arm stays at the wrist and no part of the body is
// drawn squashed between the hands. The body's unskinned groups are hidden unless a hand owns them. With show_hands
// off the whole body is hidden. What the player holds (Sly's cane) moves with the right hand, so it sits in the grip
// exactly as the animation holds it. (Entries drawn over the HUD, e.g. a title card's close-up, or into a render
// target are left as they are.)
//
// The hands are found by themselves: the right hand is a record the game names as the hand (Bentley's swing hand),
// else the bone nearest the held object (Sly's cane), else a guess (the highest pair of mirrored branches with bones
// under them: Murray); the left one is the bone at the same place in the skeleton's other branch. SLY2N_VR_BONES=1
// prints the bones (an index each) and the choice; Sly2VR.ini's hands = left,right (those indices) overrides it.

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <utility>
#include <vector>

struct SCENE;
struct ALO;
struct LO;
struct XF;
struct CAMERA;

// How the hands are found (vrprofile.h: the playing character's hands and forearm; game.cpp: the record the game
// names as the right hand, when it has one).
struct VRHANDSCFG
{
    int iboneL = -1, iboneR = -1;    // the printed indices (-1: found)
    bool fForearm = true;
    const XF* pxfSeedR = nullptr;    // (Bentley: his swing's hand, BENTLEY +0x10B8)
    uint32_t serialWorld = 0;        // (the world loaded: a new one finds the hands again)
    bool fShowHands = true;          // false: the hands hidden with the body, only the held object drawn (on the hand)
    const LO* ploCanePoint = nullptr;  // (Sly: SLY +0x19A4, the cane's hook point: which end of what he holds is the hook)
};

// The scene entries changed for this frame's draw, as they were.
struct VRHANDSSAVE
{
    std::vector<std::pair<size_t, std::vector<uint8_t>>> avis;
    std::vector<std::tuple<size_t, std::vector<glm::mat4>, std::vector<std::vector<glm::mat4>>>> amat;
    std::vector<std::tuple<size_t, bool, glm::vec3, float>> abounds;   // (the whole-object sphere, moved with what it holds)
};

// paloPlayer's entries in scene (apalo: each entry's object) drawn as its hands at the controllers seen from camEye;
// ploHeld (null: none) moved with the right hand. Without tracked hands the body is only hidden.
// (vr.h FHandCalibrating: the hands left where the animation draws them, and offered to the calibration)
void ApplyVrHands(SCENE& scene, const std::vector<ALO*>& apalo, ALO* paloPlayer, LO* ploHeld, const CAMERA& camEye,
                  const VRHANDSCFG& cfg, VRHANDSSAVE* psave);
// The entries back as they were (after the frame's draw, before the next step).
void RestoreVrHands(SCENE& scene, VRHANDSSAVE* psave);
// (0.1.2) Where the drawn cane's hook was in the world in the last drawn frame (vrinteract: a pocket hooked with the
// cane, a pole reached with it); false: no cane drawn in the hand.
bool FVrCaneHook(glm::vec3* ppos);
// (debug) The player's skeleton and what Sly holds, written to szPath (game.cpp SLY2N_SKELDUMP).
struct SW;
void DumpVrSkeleton(SW* psw, const char* szPath);
