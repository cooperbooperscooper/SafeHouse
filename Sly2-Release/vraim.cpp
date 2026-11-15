// (PC, VR) The motion controller's aim for the game's aim systems (vraim.h).
#include "vraim.h"
#include "vr.h"
#include "bsp.h"
#include "cm.h"
#include "lo.h"
#include "so.h"

#include <cmath>
#include <vector>

namespace
{
bool FUnder(const LO* plo, const LO* ploRoot)
{
    for (int i = 0; plo && i < 128; plo = plo->paloParent, ++i)
        if (plo == ploRoot) return true;
    return false;
}
}  // namespace

bool FVrAim(glm::vec3* ppos, glm::vec3* pdir)
{
    return vr::FAimRay(1, ppos, pdir);
}

glm::vec3 PosVrAimHit(const glm::vec3& pos, const glm::vec3& dir, float sMax, const LO* ploIgnore, bool* pfHit,
                      const LO* ploIgnore2)
{
    const glm::vec3 posEnd = pos + dir * sMax;
    std::vector<SOE*> apsoe;
    for (SOE* pe : AsolidWithTree(pos, posEnd))
        if ((!ploIgnore || !FUnder(pe->palo, ploIgnore)) && (!ploIgnore2 || !FUnder(pe->palo, ploIgnore2))) apsoe.push_back(pe);
    LSG lsg;
    const bool fHit = PsoeHitTestLineObjects(1, pos, posEnd, apsoe, &lsg) != nullptr;   // (as FireBentleyDart's test)
    if (pfHit) *pfHit = fHit;
    return fHit ? lsg.pos0 : posEnd;
}

bool FVrAimGround(const glm::vec3& pos, const glm::vec3& dir, float z, float sMax, glm::vec3* ppos)
{
    // (Z up: the plane z = const)
    if (std::fabs(dir.z) < 1e-5f) return false;
    const float t = (z - pos.z) / dir.z;
    if (t <= 0.0f || t > sMax) return false;
    *ppos = pos + dir * t;
    return true;
}

void ShowVrAim(const glm::vec3& posFrom, const glm::vec3& posTo)
{
    vr::NoteAimUsed(posFrom, posTo);
}
