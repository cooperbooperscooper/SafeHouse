#pragma once

// (PC, VR: vr.h) The motion controller's aim for the game's aim systems: Bentley's darts (gang.cpp), throws
// (jtthrow.cpp), the turrets (turret.cpp), the binocucom (cplcy.cpp, by the head). Each system asks FVrAim first and
// keeps its own aim (the camera, the sticks, the mouse) when it says no: a normal launch is unchanged.
//
//   if (glm::vec3 pos, dir; FVrAim(&pos, &dir)) { ... aim along the ray ...; ShowVrAim(pos, posWhereItLands); }

#include <glm/glm.hpp>

struct LO;

// The right controller's aim ray in the world this step (origin in world units, unit direction). False: no headset
// eyes drawn (stereo / first person), no motion controller, or it is not tracked.
bool FVrAim(glm::vec3* ppos, glm::vec3* pdir);
// Where the ray first meets a solid (not ploIgnore's nor ploIgnore2's, nor their parts'), at most sMax away; else its
// far end. *pfHit (optional): whether it met one. (ploIgnore2: what the aimer holds, when it is not one of its parts.)
glm::vec3 PosVrAimHit(const glm::vec3& pos, const glm::vec3& dir, float sMax, const LO* ploIgnore, bool* pfHit = nullptr,
                      const LO* ploIgnore2 = nullptr);
// Where the ray meets the horizontal plane at height z, at most sMax away (false: it points away from the plane or
// meets it farther).
bool FVrAimGround(const glm::vec3& pos, const glm::vec3& dir, float z, float sMax, glm::vec3* ppos);
// The pointer drawn along the aim this step, from posFrom to posTo (vr.h NoteAimUsed).
void ShowVrAim(const glm::vec3& posFrom, const glm::vec3& posTo);
