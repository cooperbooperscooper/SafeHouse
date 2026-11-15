#pragma once

// (PC, VR: vr.h) Messages drawn over the VR HUD layer (and the window's mirror): the T-pose calibration's
// instructions. Centred on the 4:3 canvas on a dark plate, in the game's own comic lettering (the HUD's font), over
// every screen object; uProgress (0..1) a bar filling under the lines (-1: none).

#include <string>
#include <vector>

void DrawVrMessage(const std::vector<std::string>& alines, float uProgress = -1.0f);
// (0.1.2) The wristwatch's face (vr.h FWatchState) over the whole 4:3 canvas, for vr::BeginWatchLayer / SubmitWatch:
// the gadget names (the one in use marked, the one the left hand points at lit), in the game's lettering.
void DrawVrWatch(const std::vector<std::string>& astr, int iCur, int iHover);
// The watch's entry at (u, v), the canvas's fraction from its top left, for c entries, as DrawVrWatch lays them out
// (vr.cpp: the left hand's laser on the watch); -1: none there.
int IWatchEntryAt(float u, float v, int c);
