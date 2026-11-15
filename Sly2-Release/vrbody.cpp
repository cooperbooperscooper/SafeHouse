// (PC, VR first person) The player drawn as its hands only (vrbody.h).
#include "vrbody.h"
#include "vr.h"
#include "bone.h"
#include "glob.h"
#include "lo.h"
#include "render.h"
#include "scene.h"
#include "cls.h"
#include "po.h"
#include "sly.h"
#include "actor.h"
#include "sw.h"
#include "alo.h"
#include "gang.h"
#include "activate.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace
{
constexpr int kDepthMax = 128;   // (a guard: the records' references form a tree)

bool FUnderLo(const LO* plo, const LO* ploRoot)
{
    for (int i = 0; plo && i < kDepthMax; plo = plo->paloParent, ++i)
        if (plo == ploRoot) return true;
    return false;
}

bool FUnderXf(const XF* pxf, const XF* pxfRoot)
{
    if (!pxfRoot) return false;
    for (int i = 0; pxf && i < kDepthMax; pxf = pxf->pxfRef, ++i)
        if (pxf == pxfRoot) return true;
    return false;
}

const XF* PxfUp(const XF* pxf, int c)
{
    for (int i = 0; pxf && i < c; ++i) pxf = pxf->pxfRef;
    return pxf;
}

int Depth(const XF* pxf)
{
    int c = 0;
    for (; pxf && pxf->pxfRef && c < kDepthMax; pxf = pxf->pxfRef) ++c;
    return c;
}

// scenesw.cpp's rule: a group without bones of its own uses its template's bindings
int IbindOf(const GLOBSET& gs, size_t i)
{
    const GLOB& g = gs.aglob[i];
    return g.iglobTemplate >= 0 && g.abone.empty() ? g.iglobTemplate : int(i);
}

// The hands found for a model (kept while the player, its bone count and the configuration stay the same).
struct HANDS
{
    const ALO* palo = nullptr;
    size_t cbone = 0;
    VRHANDSCFG cfg;
    const XF* apxfHand[2]{};   // 0 left, 1 right
    const XF* apxfArm[2]{};    // each hand's forearm, moved with it with forearm = 1 (null: none)
    const char* szHow = "";
};
HANDS s_hands;

using MPBIND = std::unordered_map<const XF*, glm::mat4>;

bool FSameCfg(const VRHANDSCFG& a, const VRHANDSCFG& b)
{
    return a.iboneL == b.iboneL && a.iboneR == b.iboneR && a.fForearm == b.fForearm && a.pxfSeedR == b.pxfSeedR &&
           a.serialWorld == b.serialWorld;
}

// A bound record's place in the bind pose (the model's space: Z up, -Y the character's right)
glm::vec3 PosBind(const MPBIND& mpbind, const XF* pxf) { return glm::vec3(glm::inverse(mpbind.at(pxf))[3]); }

// The hand on side s (0 left, 1 right) of a model bound in a T-pose (Sly's, Bentley's and Murray's are: the hands level
// with the shoulders, out to the sides; measured on the disc): where that arm ends, the record common to the bones
// reaching furthest out (Murray's three fingers: his hand) when it is bound and still well out, else the furthest bone
const XF* PxfArmEnd(const std::vector<const XF*>& abone, const MPBIND& mpbind, int s)
{
    const float sgn = s == 1 ? -1.0f : 1.0f;
    float yMax = 0.0f;
    for (const XF* pxf : abone) yMax = std::max(yMax, sgn * PosBind(mpbind, pxf).y);
    if (yMax < 1.0f) return nullptr;
    std::vector<const XF*> aend;
    for (const XF* pxf : abone)
        if (sgn * PosBind(mpbind, pxf).y >= 0.85f * yMax) aend.push_back(pxf);
    const XF* pxfFar = aend[0];
    for (const XF* pxf : aend)
        if (sgn * PosBind(mpbind, pxf).y > sgn * PosBind(mpbind, pxfFar).y) pxfFar = pxf;
    const XF* pxfCommon = nullptr;
    int i = 0;
    for (const XF* pxf = aend[0]; pxf && !pxfCommon && i < kDepthMax; pxf = pxf->pxfRef, ++i)
        if (std::all_of(aend.begin(), aend.end(), [&](const XF* o) { return FUnderXf(o, pxf); })) pxfCommon = pxf;
    i = 0;
    for (const XF* pxf = pxfCommon; pxf && i < kDepthMax; pxf = pxf->pxfRef, ++i)
        if (mpbind.count(pxf)) return sgn * PosBind(mpbind, pxf).y >= 0.4f * yMax ? pxf : pxfFar;
    return pxfFar;
}

// Hand pxfHand's forearm: the bound bone on its side, not under it, nearer the body, nearest to it in the bind pose
const XF* PxfForearm(const std::vector<const XF*>& abone, const MPBIND& mpbind, const XF* pxfHand, int s)
{
    if (!pxfHand) return nullptr;
    const float sgn = s == 1 ? -1.0f : 1.0f;
    const glm::vec3 posHand = PosBind(mpbind, pxfHand);
    const XF* pxfBest = nullptr;
    float dBest = 0.6f * sgn * posHand.y;
    for (const XF* pxf : abone)
    {
        if (pxf == pxfHand || FUnderXf(pxf, pxfHand)) continue;
        const glm::vec3 p = PosBind(mpbind, pxf);
        if (sgn * p.y <= 0.0f || sgn * p.y >= sgn * posHand.y) continue;
        if (const float d = glm::length(p - posHand); d < dBest) dBest = d, pxfBest = pxf;
    }
    return pxfBest;
}

// The hands: Sly2VR.ini's indices, else the game's own hand record (Bentley's swing hand) when it is a bone, else the
// record what is held hangs from (Sly's cane: his right hand), else where each arm ends in the bind pose
void FindHands(const std::vector<const XF*>& abone, const MPBIND& mpbind, const XF* pxfHeldAt, const ALO* paloPlayer,
               const VRHANDSCFG& cfg)
{
    s_hands = HANDS{};
    s_hands.palo = paloPlayer;
    s_hands.cbone = abone.size();
    s_hands.cfg = cfg;
    auto byIndex = [&](int i) { return i >= 0 && size_t(i) < abone.size() ? abone[size_t(i)] : nullptr; };
    const XF* pxfR = byIndex(cfg.iboneR);
    if (pxfR) s_hands.szHow = "Sly2VR.ini hands";
    if (!pxfR && cfg.pxfSeedR && mpbind.count(cfg.pxfSeedR) && PosBind(mpbind, cfg.pxfSeedR).y < 0.0f)
        pxfR = cfg.pxfSeedR, s_hands.szHow = "the game's hand record";
    if (!pxfR && pxfHeldAt)
    {
        int i = 0;
        for (const XF* pxf = pxfHeldAt; pxf && !pxfR && i < kDepthMax; pxf = pxf->pxfRef, ++i)
            if (mpbind.count(pxf)) pxfR = pxf;
        if (pxfR && PosBind(mpbind, pxfR).y >= 0.0f) pxfR = nullptr;   // (not on the right: not a hand)
        if (pxfR) s_hands.szHow = "what it holds";
    }
    if (!pxfR && (pxfR = PxfArmEnd(abone, mpbind, 1))) s_hands.szHow = "the bind pose (T-pose)";
    const XF* pxfL = byIndex(cfg.iboneL);
    if (!pxfL) pxfL = PxfArmEnd(abone, mpbind, 0);
    s_hands.apxfHand[0] = pxfL;
    s_hands.apxfHand[1] = pxfR;
    for (int s = 0; s < 2; ++s)
        s_hands.apxfArm[s] = cfg.fForearm ? PxfForearm(abone, mpbind, s_hands.apxfHand[s], s) : nullptr;
    if (std::getenv("SLY2N_VR_BONES"))
    {
        std::printf("vr hands: %zu bones of the player (oid %d, %s); the bind pose (model space: Z up, -Y right), depth, bones under it:\n",
                    abone.size(), int(paloPlayer->oid), vrprofile::SzChar(vr::CharacterCur()));
        for (size_t i = 0; i < abone.size(); ++i)
        {
            const glm::vec3 b = PosBind(mpbind, abone[i]);
            int c = 0;
            for (const XF* o : abone) c += o != abone[i] && FUnderXf(o, abone[i]);
            const char* szMark = abone[i] == s_hands.apxfHand[0] ? "  <- left hand" : abone[i] == s_hands.apxfHand[1] ? "  <- right hand"
                               : abone[i] == s_hands.apxfArm[0] ? "  <- left forearm" : abone[i] == s_hands.apxfArm[1] ? "  <- right forearm" : "";
            std::printf("  %3zu: %8.1f %8.1f %8.1f  depth %2d, %2d under%s\n", i, b.x, b.y, b.z, Depth(abone[i]), c, szMark);
        }
        if (!s_hands.apxfHand[0] || !s_hands.apxfHand[1])
            std::printf("vr hands: not found (set hands = left,right under [%s] in Sly2VR.ini)\n", vrprofile::SzChar(vr::CharacterCur()));
        else std::printf("vr hands: the right found by %s, the left by the bind pose (wrong? set hands = left,right under [%s] in Sly2VR.ini)\n",
                         s_hands.szHow, vrprofile::SzChar(vr::CharacterCur()));
    }
}

// The character's hands in its own T-pose, for vr.h SetCharacterTPose: its bind pose (the model's space, Z up), a
// forearm bound lowered or raised by more than 15 degrees (an A-pose) turned level, each hand's turn in the
// character's body axes (right: from its left hand to its right, up, back)
void CharacterTPose(const MPBIND& mpbind)
{
    const bool fLog = std::getenv("SLY2N_VR_BONES") != nullptr;
    auto bindOf = [&](const XF* pxf, glm::mat4* pm) {
        auto it = pxf ? mpbind.find(pxf) : mpbind.end();
        if (it == mpbind.end()) return false;
        *pm = glm::inverse(it->second);   // (the bone's pose in the model's space)
        return true;
    };
    glm::mat4 amat[2];
    if (!bindOf(s_hands.apxfHand[0], &amat[0]) || !bindOf(s_hands.apxfHand[1], &amat[1]))
    {
        vr::SetCharacterTPose(nullptr);
        if (fLog) std::printf("vr hands: T-pose: the hands have no bind pose (hand_rot is used)\n");
        return;
    }
    const glm::vec3 apos[2]{glm::vec3(amat[0][3]), glm::vec3(amat[1][3])};
    glm::vec3 d = apos[1] - apos[0];
    const float sSpan = glm::length(d);
    // (both hands at about one height, apart sideways: else the model's space is not Z up as assumed)
    if (sSpan < 1e-3f || std::fabs(d.z) > 0.35f * sSpan)
    {
        vr::SetCharacterTPose(nullptr);
        if (fLog) std::printf("vr hands: T-pose: the bind pose's hands are not level (hand_rot is used)\n");
        return;
    }
    d.z = 0.0f;
    const glm::vec3 right = glm::normalize(d), up(0.0f, 0.0f, 1.0f), back = glm::cross(right, up);
    const glm::mat3 matBody(right, up, back);
    glm::mat3 amatT[2];
    for (int k = 0; k < 2; ++k)
    {
        glm::mat3 r(amat[k]);
        for (int i = 0; i < 3; ++i) r[i] = glm::normalize(r[i]);
        if (glm::determinant(r) < 0.0f)
        {
            vr::SetCharacterTPose(nullptr);
            if (fLog) std::printf("vr hands: T-pose: a hand's bind pose is mirrored (hand_rot is used)\n");
            return;
        }
        glm::mat4 matArm(1.0f);
        if (s_hands.apxfArm[k] && bindOf(s_hands.apxfArm[k], &matArm))
        {
            const glm::vec3 dirArm = glm::normalize(apos[k] - glm::vec3(matArm[3]));
            const float radElev = std::asin(std::clamp(dirArm.z, -1.0f, 1.0f));
            const glm::vec3 dirLevel(dirArm.x, dirArm.y, 0.0f);
            if (std::fabs(radElev) > glm::radians(15.0f) && glm::length(dirLevel) > 1e-3f)
            {
                const glm::vec3 axis = glm::cross(dirArm, glm::normalize(dirLevel));
                if (glm::length(axis) > 1e-4f) r = glm::mat3_cast(glm::angleAxis(std::fabs(radElev), glm::normalize(axis))) * r;
                if (fLog) std::printf("vr hands: T-pose: the %s forearm bound %.0f degrees off level: turned level\n", k ? "right" : "left",
                                      glm::degrees(radElev));
            }
        }
        amatT[k] = glm::transpose(matBody) * r;
    }
    vr::SetCharacterTPose(amatT);
    if (fLog) std::printf("vr hands: T-pose: from the bind pose (hands %.1f apart)\n", sSpan);
}

// What Sly holds (his cane) by its own shape, measured once per world from its vertices, in the space of its first
// drawn group's matrix: the frame its grip point sits at (origin where the hand holds it, -Z along the shaft toward the
// hook, +Y away from the side the hook curls to), and the hook's point
struct CANEGEO
{
    const LO* plo = nullptr;
    uint32_t serial = 0;
    bool fOk = false;
    glm::mat4 matFrame{1.0f};
    glm::vec3 posHook{0.0f};
    float sLength = 0.0f;
};
CANEGEO s_cane;
bool s_fCaneHook = false;
glm::vec3 s_posCaneHook{0.0f};

void MeasureCaneVertices(const std::vector<glm::vec3>& apos, const glm::vec3& posGrip, const glm::vec3* pposPoint, CANEGEO* pg);
void MeasureCane(const SCENE& scene, const std::vector<size_t>& akHeld, const glm::mat4& matRef, const glm::vec3& posGripWorld,
                 const glm::vec3* pposPointWorld, CANEGEO* pg)
{
    pg->fOk = false;
    const glm::mat4 matInv = glm::inverse(matRef);
    std::vector<glm::vec3> apos;
    for (size_t k : akHeld)
    {
        const SCENEGLOBSET& sgs = scene.aglobset[k];
        const GLOBSET& gs = *sgs.pglobset;
        for (size_t i = 0; i < gs.aglob.size() && i < sgs.amatGlob.size(); ++i)
        {
            if (i < sgs.aamatBone.size() && !sgs.aamatBone[i].empty()) continue;   // (skinned: not the cane's rigid shape)
            const GLOB& glob = gs.aglob[i];
            const GLOB& geom = !glob.avtx.empty() || glob.iglobTemplate < 0 ? glob : gs.aglob[size_t(glob.iglobTemplate)];
            const glm::mat4 m = matInv * sgs.amatGlob[i];
            for (const VTXGLOB& v : geom.avtx) apos.push_back(glm::vec3(m * glm::vec4(v.pos, 1.0f)));
        }
    }
    const glm::vec3 posGrip(matInv * glm::vec4(posGripWorld, 1.0f));
    const glm::vec3 posPoint = pposPointWorld ? glm::vec3(matInv * glm::vec4(*pposPointWorld, 1.0f)) : glm::vec3(0.0f);
    MeasureCaneVertices(apos, posGrip, pposPointWorld ? &posPoint : nullptr, pg);
}

// (the measure itself, in any space rigid with the cane: its vertices, the hand's record, the hook point)
void MeasureCaneVertices(const std::vector<glm::vec3>& apos, const glm::vec3& posGrip, const glm::vec3* pposPoint, CANEGEO* pg)
{
    pg->fOk = false;
    if (apos.size() < 8) return;
    // the main axis of all of it (power iteration), and a line through a subset's centre along its own main axis
    auto fit = [](const std::vector<glm::vec3>& ap, glm::vec3* pc, glm::vec3* pa) {
        glm::vec3 c(0.0f);
        for (const glm::vec3& p : ap) c += p;
        c /= float(ap.size());
        glm::mat3 cov(0.0f);
        for (const glm::vec3& p : ap) cov += glm::outerProduct(p - c, p - c);
        glm::vec3 a = glm::normalize(glm::vec3(1.0f, 0.3f, 0.1f));
        for (int it = 0; it < 64; ++it) a = glm::normalize(cov * a);
        *pc = c, *pa = a;
    };
    glm::vec3 c, a;
    fit(apos, &c, &a);
    float tMin = 1e9f, tMax = -1e9f;
    for (const glm::vec3& p : apos) tMin = std::min(tMin, glm::dot(p - c, a)), tMax = std::max(tMax, glm::dot(p - c, a));
    const float sLen = tMax - tMin;
    if (sLen < 10.0f) return;
    // the shaft: the cane's one long straight run (measured on the disc: a thin rod about 135 long, at about 59 degrees
    // to the whole cane's main axis, which the crook's many vertices pull away, and with the crook next to the hand):
    // the line through two of its vertices that the most vertices lie on, along the longest stretch, then fitted to them
    const float sOnLine = std::max(2.5f, 0.016f * sLen);
    const size_t cv = apos.size(), dv = std::max<size_t>(1, cv / 90);
    int cBest = 0;
    float spanBest = 0.0f;
    glm::vec3 cs = c, s_ = a;
    for (size_t i = 0; i < cv; i += dv)
        for (size_t j = i + 1; j < cv; j += dv)
        {
            const glm::vec3 d = apos[j] - apos[i];
            if (glm::length(d) < 0.3f * sLen) continue;
            const glm::vec3 u = glm::normalize(d);
            int cn = 0;
            float t0 = 1e9f, t1 = -1e9f;
            for (const glm::vec3& p : apos)
            {
                const glm::vec3 q = p - apos[i];
                const float t = glm::dot(q, u);
                if (glm::length(q - t * u) < sOnLine) ++cn, t0 = std::min(t0, t), t1 = std::max(t1, t);
            }
            if (cn > cBest || (cn == cBest && t1 - t0 > spanBest)) cBest = cn, spanBest = t1 - t0, cs = apos[i], s_ = u;
        }
    std::vector<glm::vec3> aline;
    for (int it = 0; it < 2; ++it)
    {
        std::vector<glm::vec3> ain;
        for (const glm::vec3& p : apos)
            if (glm::length((p - cs) - glm::dot(p - cs, s_) * s_) < sOnLine * (it ? 1.0f : 1.5f)) ain.push_back(p);
        if (ain.size() < 4) break;
        glm::vec3 c2, s2;
        fit(ain, &c2, &s2);
        cs = c2, s_ = s2;
        aline.swap(ain);
    }
    // toward the hook: where the game's cane point is (Sly's +0x19A4, the hook that catches poles and pockets), else
    // toward the cane's centre (the crook's mass)
    const bool fPoint = pposPoint != nullptr;
    const glm::vec3 posPoint = fPoint ? *pposPoint : glm::vec3(0.0f);
    const glm::vec3 posToward = fPoint ? posPoint : c;
    if (glm::dot(posToward - cs, s_) < 0.0f) s_ = -s_;
    // the grip: the hand's record brought onto the shaft; the plain end: the shaft's far end the other way
    const glm::vec3 posG = cs + glm::dot(posGrip - cs, s_) * s_;
    float sPlain = 0.0f;
    for (const glm::vec3& p : aline) sPlain = std::max(sPlain, -glm::dot(p - posG, s_));
    // which way the crook curls: the hook point's side of the shaft, else the side of the vertices off the shaft
    glm::vec3 curl = fPoint ? (posPoint - cs) - glm::dot(posPoint - cs, s_) * s_ : glm::vec3(0.0f);
    if (glm::length(curl) < 0.05f * sLen)
    {
        glm::vec3 sum(0.0f);
        int cn = 0;
        for (const glm::vec3& p : apos)
            if (const glm::vec3 d = (p - cs) - glm::dot(p - cs, s_) * s_; glm::length(d) >= sOnLine) sum += d, ++cn;
        if (cn) curl = sum / float(cn);
    }
    const glm::vec3 posCrook = fPoint ? posPoint : c;
    const glm::vec3 z = -s_;
    glm::vec3 y = glm::length(curl) > 0.01f * sLen ? -glm::normalize(curl) : glm::normalize(glm::cross(z, glm::vec3(0.0f, 0.0f, 1.0f)));
    if (!(glm::length(y) > 0.5f)) y = glm::normalize(glm::cross(z, glm::vec3(1.0f, 0.0f, 0.0f)));
    const glm::vec3 x = glm::normalize(glm::cross(y, z));
    y = glm::cross(z, x);
    pg->matFrame = glm::mat4(glm::vec4(x, 0.0f), glm::vec4(y, 0.0f), glm::vec4(z, 0.0f), glm::vec4(posG, 1.0f));
    pg->posHook = fPoint ? posPoint : posCrook;
    pg->sLength = sLen;
    pg->fOk = true;
    if (std::getenv("SLY2N_VR_BONES"))
        std::printf("vr cane: %zu vertices, %.1f across; the shaft fitted on %zu, %.1f from the hand's record; the hook (%s) %.1f out "
                    "along it, %.1f to the crook's side; the plain end %.1f the other way\n", apos.size(), sLen, aline.size(),
                    glm::length(posGrip - posG), fPoint ? "the cane point" : "the crook's centre", glm::dot(posCrook - posG, s_),
                    glm::length(curl), sPlain);
}

void Save(SCENE& scene, size_t k, VRHANDSSAVE* psave)
{
    const SCENEGLOBSET& sgs = scene.aglobset[k];
    psave->avis.emplace_back(k, sgs.afVisible);
    psave->amat.emplace_back(k, sgs.amatGlob, sgs.aamatBone);
    psave->abounds.emplace_back(k, sgs.fBounds, sgs.posBounds, sgs.sBoundsRadius);
}

void Hide(SCENEGLOBSET& sgs) { std::fill(sgs.afVisible.begin(), sgs.afVisible.end(), uint8_t(0)); }

// What the player holds moved by mat (its groups, its bones and its whole-object sphere).
void MoveHeld(SCENEGLOBSET& sgs, const glm::mat4& mat)
{
    for (glm::mat4& m : sgs.amatGlob) m = mat * m;
    for (auto& amatBone : sgs.aamatBone)
        for (glm::mat4& m : amatBone) m = mat * m;
    sgs.posBounds = glm::vec3(mat * glm::vec4(sgs.posBounds, 1.0f));
}
}  // namespace

void ApplyVrHands(SCENE& scene, const std::vector<ALO*>& apalo, ALO* paloPlayer, LO* ploHeld, const CAMERA& camEye,
                  const VRHANDSCFG& cfg, VRHANDSSAVE* psave)
{
    s_fCaneHook = false;
    if (!paloPlayer) return;
    std::vector<size_t> akBody, akHeld, akTrail;
    for (size_t k = 0; k < scene.aglobset.size() && k < apalo.size(); ++k)
    {
        if (!apalo[k] || !scene.aglobset[k].pglobset) continue;
        // (a title card's close-up drawn over the HUD, a portrait drawn into a render target: not the player's body)
        if (scene.aglobset[k].fForeground || scene.aglobset[k].irtgt >= 0) continue;
        // (a trail under what is held keeps its own world-space vertices: it would follow the cane the animation
        // swings, not the one in your hand: hidden)
        if (ploHeld && FUnderLo(apalo[k], ploHeld)) (apalo[k]->Cid() == 0x69 ? akTrail : akHeld).push_back(k);
        else if (FUnderLo(apalo[k], paloPlayer)) akBody.push_back(k);
    }

    // the model's bones as drawn this frame: each record's world, from its skinning matrix (M = world x bind)
    std::vector<const XF*> abone;
    MPBIND mpmatWorld, mpmatBind;
    for (size_t k : akBody)
    {
        const auto* paabind = PaabindOfAlo(apalo[k]);
        const SCENEGLOBSET& sgs = scene.aglobset[k];
        if (!paabind) continue;
        for (size_t i = 0; i < sgs.aamatBone.size() && i < sgs.pglobset->aglob.size(); ++i)
        {
            const auto& amat = sgs.aamatBone[i];
            const int ib = IbindOf(*sgs.pglobset, i);
            if (amat.empty() || ib < 0 || size_t(ib) >= paabind->size() || (*paabind)[size_t(ib)].size() != amat.size()) continue;
            const auto& abind = (*paabind)[size_t(ib)];
            for (size_t j = 0; j < abind.size(); ++j)
                if (const XF* pxf = abind[j].pxfBone; pxf && !mpmatWorld.count(pxf))
                {
                    mpmatWorld[pxf] = amat[j] * glm::inverse(abind[j].matBind);
                    mpmatBind[pxf] = abind[j].matBind;
                    abone.push_back(pxf);
                }
        }
    }
    // (the record what is held hangs from: Sly's cane's first record's parent, his right hand)
    const ALO* paloHeld = ploHeld && FCidDerivedFrom(ploHeld->Cid(), 0x30) ? static_cast<const ALO*>(ploHeld) : nullptr;
    const XF* pxfHeldAt = paloHeld && !paloHeld->axf.empty() ? paloHeld->axf[0]->pxfRef : nullptr;
    // (found again for another player, model, configuration or world, or when a hand is no longer one of its bones)
    if (s_hands.palo != paloPlayer || s_hands.cbone != abone.size() || !FSameCfg(s_hands.cfg, cfg) ||
        (s_hands.apxfHand[0] && !mpmatWorld.count(s_hands.apxfHand[0])) || (s_hands.apxfHand[1] && !mpmatWorld.count(s_hands.apxfHand[1])))
    {
        FindHands(abone, mpmatBind, pxfHeldAt, paloPlayer, cfg);
        CharacterTPose(mpmatBind);
    }

    // each hand's move this frame: from where the animation drew it onto the controller
    glm::mat4 amatMove[2]{glm::mat4(1.0f), glm::mat4(1.0f)};
    glm::vec3 aposHand[2]{};
    bool afHand[2]{};
    glm::mat4 amatCtrl[2]{glm::mat4(1.0f), glm::mat4(1.0f)};   // (0.1.4: each hand's tuned controller matrix)
    bool afCtrl[2]{};
    const bool fCal = vr::FHandCalibrating();
    for (int s = 0; s < 2; ++s)
    {
        const XF* pxf = s_hands.apxfHand[s];
        if (pxf && mpmatWorld.count(pxf) && fCal)
        {
            // the calibration: the hand where the animation holds it (offered for the capture), not on the controller
            vr::OfferHandPose(s, mpmatWorld[pxf]);
            aposHand[s] = glm::vec3(mpmatWorld[pxf][3]);
            afHand[s] = true;
            continue;
        }
        glm::mat4 matCtrl(1.0f);
        if (!pxf || !mpmatWorld.count(pxf) || !vr::FHandWorld(s, camEye, &matCtrl)) continue;
        // (0.1.3: hand_size: the glove drawn smaller or larger about its own origin, the wrist)
        amatMove[s] = matCtrl * glm::scale(glm::mat4(1.0f), glm::vec3(vr::ProfileCur().handSize)) * glm::inverse(mpmatWorld[pxf]);
        aposHand[s] = glm::vec3(mpmatWorld[pxf][3]);
        afHand[s] = true;
        amatCtrl[s] = matCtrl;
        afCtrl[s] = true;
    }

    // the cane: in the right grip by its own shape (the shaft along your fist, the hook out over the thumb and curling
    // forward, Sly2VR.ini cane_rot / cane_offset / cane_size), the right hand closed on it as the animation holds it;
    // holstered: across the back behind the right shoulder; else (no grip tracked) hidden. (The calibration leaves it
    // in the animation's hand.)
    if (!akHeld.empty() && !fCal)
    {
        size_t kRef = akHeld[0];
        for (size_t k : akHeld)
            if (apalo[k] == paloHeld && !scene.aglobset[k].amatGlob.empty()) { kRef = k; break; }
        const bool fRef = !scene.aglobset[kRef].amatGlob.empty();
        const glm::mat4 matRef = fRef ? scene.aglobset[kRef].amatGlob[0] : glm::mat4(1.0f);
        if (fRef && (s_cane.plo != ploHeld || s_cane.serial != cfg.serialWorld))
        {
            const glm::vec3 posGrip = pxfHeldAt && mpmatWorld.count(pxfHeldAt) ? glm::vec3(mpmatWorld[pxfHeldAt][3])
                                    : pxfHeldAt ? glm::vec3(pxfHeldAt->matWorld[3]) : glm::vec3(matRef[3]);
            // (the cane point is at the step's time, the drawn scene between two steps: brought into it through the hand's
            // record, which the cane is rigid with)
            glm::vec3 posPoint = cfg.ploCanePoint ? PosLoWorld(cfg.ploCanePoint) : glm::vec3(0.0f);
            if (cfg.ploCanePoint && pxfHeldAt && mpmatWorld.count(pxfHeldAt))
                posPoint = glm::vec3(mpmatWorld[pxfHeldAt] * glm::inverse(pxfHeldAt->matWorld) * glm::vec4(posPoint, 1.0f));
            MeasureCane(scene, akHeld, matRef, posGrip, cfg.ploCanePoint ? &posPoint : nullptr, &s_cane);
            s_cane.plo = ploHeld;
            s_cane.serial = cfg.serialWorld;
        }
        const vrprofile::PROFILE& pr = vr::ProfileCur();
        const bool fDrawn = vr::FCaneDrawn();
        glm::mat4 matAt(1.0f);
        const bool fAt = fRef && s_cane.fOk && (fDrawn ? vr::FGripWorld(1, camEye, &matAt) : vr::FHolsterWorld(camEye, &matAt));
        glm::mat4 matMove(1.0f);
        if (fAt)
        {
            glm::mat4 matAdj(1.0f);
            if (fDrawn)
                matAdj = glm::translate(glm::mat4(1.0f), pr.cmCane * (vr::UnitsPerMetre() / 100.0f)) *
                         glm::mat4_cast(glm::quat(glm::radians(pr.degCane)));
            const glm::mat4 matCaneNoSize = matAt * matAdj * glm::inverse(s_cane.matFrame);
            const glm::mat4 matCane = matAt * matAdj * glm::scale(glm::mat4(1.0f), glm::vec3(pr.caneSize)) * glm::inverse(s_cane.matFrame);
            matMove = matCane * glm::inverse(matRef);
            if (fDrawn)
            {
                // (the right hand closes on the cane as the animation holds it; no size: the glove keeps its own)
                if (afHand[1]) amatMove[1] = matCaneNoSize * glm::scale(glm::mat4(1.0f), glm::vec3(pr.handSize)) * glm::inverse(matRef);
                s_posCaneHook = glm::vec3(matCane * glm::vec4(s_cane.posHook, 1.0f));
                s_fCaneHook = true;
                // (0.1.4, cane_fit = hand: the cane and the glove ride the same tuned hand matrix as the bare hands (the
                // animation holds the cane rigidly in its glove), the cane's turn / shift / size on top; the grip fit
                // above stays as the hook's place)
                const XF* pxfR = s_hands.apxfHand[1];
                if (pr.fCaneOnHand && afCtrl[1] && pxfR && mpmatWorld.count(pxfR))
                {
                    const glm::mat4 matHandMove = amatCtrl[1] * matAdj;
                    const glm::mat4 matInvHand = glm::inverse(mpmatWorld[pxfR]);
                    matMove = matHandMove * glm::scale(glm::mat4(1.0f), glm::vec3(pr.caneSize)) * matInvHand;
                    amatMove[1] = matHandMove * glm::scale(glm::mat4(1.0f), glm::vec3(pr.handSize)) * matInvHand;
                }
            }
        }
        else if (!s_cane.fOk && fDrawn && afHand[1]) matMove = amatMove[1];   // (not measured: with the hand, as 0.1.1)
        for (size_t k : akHeld)
        {
            SCENEGLOBSET& sgs = scene.aglobset[k];
            Save(scene, k, psave);
            if (fAt || (!s_cane.fOk && fDrawn && afHand[1]))
            {
                MoveHeld(sgs, matMove);
                if (pr.caneSize > 1.0f) sgs.sBoundsRadius *= pr.caneSize;
            }
            else Hide(sgs);
        }
        for (size_t k : akTrail)
        {
            Save(scene, k, psave);
            Hide(scene.aglobset[k]);
        }
    }

    // the body: hidden outright with the hands off or none tracked (no bone squashed to a point: linear skinning
    // averages such points into a ghost of the body)
    if (!cfg.fShowHands || (!afHand[0] && !afHand[1]))
    {
        for (size_t k : akBody)
        {
            Save(scene, k, psave);
            Hide(scene.aglobset[k]);
        }
        return;
    }

    // which hand a record moves with (-1: neither): the hand's own records, and its forearm
    auto side = [&](const XF* pxf) {
        for (int s = 1; s >= 0; --s)
            if (afHand[s] && (FUnderXf(pxf, s_hands.apxfHand[s]) || (s_hands.apxfArm[s] && pxf == s_hands.apxfArm[s]))) return s;
        return -1;
    };
    // each arm's end (its forearm's or hand's origin) where it is drawn this frame
    glm::vec3 aposEnd[2]{};
    for (int s = 0; s < 2; ++s)
        if (afHand[s])
        {
            const XF* pxfEnd = s_hands.apxfArm[s] ? s_hands.apxfArm[s] : s_hands.apxfHand[s];
            const glm::vec3 posEnd = pxfEnd && mpmatWorld.count(pxfEnd) ? glm::vec3(mpmatWorld[pxfEnd][3]) : aposHand[s];
            aposEnd[s] = glm::vec3(amatMove[s] * glm::vec4(posEnd, 1.0f));
        }
    // a record of neither hand, in a group with hand bones: closed up at the end of its own side's arm (by the bind
    // pose: the upper arm, the shoulder), else at the nearer tracked one (a tiny scale, not zero: a normal or an inverse
    // taken from the matrix stays finite)
    auto collapse = [&](const XF* pxf, const glm::vec3& pos) {
        int s = -1;
        if (pxf && mpmatBind.count(pxf))
        {
            const float y = PosBind(mpmatBind, pxf).y;
            const int sSide = y < 0.0f ? 1 : 0;
            const XF* pxfH = s_hands.apxfHand[sSide];
            if (afHand[sSide] && pxfH && mpmatBind.count(pxfH) && std::fabs(y) > 0.15f * std::fabs(PosBind(mpmatBind, pxfH).y)) s = sSide;
        }
        if (s < 0)
        {
            s = afHand[1] ? 1 : 0;
            if (afHand[0] && afHand[1] && glm::length(pos - aposHand[0]) < glm::length(pos - aposHand[1])) s = 0;
        }
        glm::mat4 m = glm::scale(glm::mat4(1.0f), glm::vec3(1e-4f));
        m[3] = glm::vec4(aposEnd[s], 1.0f);
        return m;
    };
    for (size_t k : akBody)
    {
        SCENEGLOBSET& sgs = scene.aglobset[k];
        Save(scene, k, psave);
        // (the whole-object sphere stays at the body, but the hands go wherever you reach or walk: not tested)
        sgs.fBounds = false;
        const auto* paabind = PaabindOfAlo(apalo[k]);
        for (size_t i = 0; i < sgs.amatGlob.size() && i < sgs.pglobset->aglob.size(); ++i)
        {
            const bool fSkinned = i < sgs.aamatBone.size() && !sgs.aamatBone[i].empty();
            if (!fSkinned)
            {
                // an unskinned group: on its hand when its record is one of a hand's, else hidden
                const XF* pxfOwner = i < sgs.apxfOwner.size() ? sgs.apxfOwner[i] : nullptr;
                const int s = pxfOwner ? side(pxfOwner) : -1;
                if (s >= 0) sgs.amatGlob[i] = amatMove[s] * sgs.amatGlob[i];
                else if (i < sgs.afVisible.size()) sgs.afVisible[i] = 0;
                continue;
            }
            auto& amat = sgs.aamatBone[i];
            const int ib = IbindOf(*sgs.pglobset, i);
            if (!paabind || ib < 0 || size_t(ib) >= paabind->size() || (*paabind)[size_t(ib)].size() != amat.size())
            {
                if (i < sgs.afVisible.size()) sgs.afVisible[i] = 0;
                continue;
            }
            const auto& abind = (*paabind)[size_t(ib)];
            // a group with no hand bone (the head, the body, the legs): hidden
            bool fAnyHand = false;
            for (size_t j = 0; j < abind.size() && !fAnyHand; ++j) fAnyHand = abind[j].pxfBone && side(abind[j].pxfBone) >= 0;
            if (!fAnyHand)
            {
                if (i < sgs.afVisible.size()) sgs.afVisible[i] = 0;
                continue;
            }
            for (size_t j = 0; j < amat.size(); ++j)
            {
                const XF* pxf = abind[j].pxfBone;
                const int s = pxf ? side(pxf) : -1;
                if (s >= 0) amat[j] = amatMove[s] * amat[j];
                else amat[j] = collapse(pxf, pxf && mpmatWorld.count(pxf) ? glm::vec3(mpmatWorld[pxf][3]) : glm::vec3(amat[j][3]));
            }
        }
    }
}

bool FVrCaneHook(glm::vec3* ppos)
{
    if (!s_fCaneHook) return false;
    *ppos = s_posCaneHook;
    return true;
}

void RestoreVrHands(SCENE& scene, VRHANDSSAVE* psave)
{
    for (auto& [k, af] : psave->avis) scene.aglobset[k].afVisible = std::move(af);
    for (auto& [k, amat, aamatBone] : psave->amat)
    {
        scene.aglobset[k].amatGlob = std::move(amat);
        scene.aglobset[k].aamatBone = std::move(aamatBone);
    }
    for (auto& [k, fBounds, posBounds, sRadius] : psave->abounds)
    {
        scene.aglobset[k].fBounds = fBounds;
        scene.aglobset[k].posBounds = posBounds;
        scene.aglobset[k].sBoundsRadius = sRadius;
    }
    psave->avis.clear();
    psave->amat.clear();
    psave->abounds.clear();
}

// (debug, every build: game.cpp SLY2N_SKELDUMP=file, e.g. a headless --world run) Each gang member's skeleton as its
// groups bind it (Sly, and Bentley and Murray when loaded), and what Sly holds: each bound record with its owner,
// parent, depth, children, its world place now and its bind pose in the model's space; the held object's records.
namespace
{
void DumpOneSkeleton(SW* psw, ALO* palo, LO* ploHeld, const XF* pxfSeed, const char* szWho, FILE* pf)
{
    std::fprintf(pf, "==== %s: oid %d cid 0x%X, held %d, seed record %p\n", szWho, int(palo->oid), palo->Cid(), ploHeld ? int(ploHeld->oid) : -1,
                 static_cast<const void*>(pxfSeed));
    std::unordered_map<const XF*, std::pair<int, size_t>> mpown;   // record -> (owner oid, index in its axf)
    std::vector<ALO*> aalo;
    for (const auto& up : psw->aploAll)
    {
        LO* plo = up.get();
        if (!FCidDerivedFrom(plo->Cid(), 0x30)) continue;
        ALO* pa = static_cast<ALO*>(plo);
        if (!FUnderLo(pa, palo) && !(ploHeld && FUnderLo(pa, ploHeld))) continue;
        aalo.push_back(pa);
        for (size_t r = 0; r < pa->axf.size(); ++r) mpown[pa->axf[r].get()] = {int(pa->oid), r};
    }
    auto name = [&](const XF* pxf) {
        char ach[48];
        if (!pxf) return std::string("none");
        auto it = mpown.find(pxf);
        if (it == mpown.end()) std::snprintf(ach, sizeof ach, "ext(%d)", pxf->palo ? int(pxf->palo->oid) : -1);
        else std::snprintf(ach, sizeof ach, "%d:%zu", it->second.first, it->second.second);
        return std::string(ach);
    };
    if (pxfSeed) std::fprintf(pf, "seed record = %s\n", name(pxfSeed).c_str());
    std::vector<const XF*> abone;
    std::unordered_map<const XF*, glm::mat4> mpbind;
    for (ALO* pa : aalo)
    {
        const bool fHeld = ploHeld && FUnderLo(pa, ploHeld);
        const auto* paabind = PaabindOfAlo(pa);
        std::fprintf(pf, "\nalo oid %d cid 0x%X %s parent %d: %zu records, %zu bound groups\n", int(pa->oid), pa->Cid(),
                     fHeld ? "HELD" : "body", pa->paloParent ? int(pa->paloParent->oid) : -1, pa->axf.size(), paabind ? paabind->size() : size_t(0));
        if (pa != palo || std::getenv("SLY2N_SKELDUMP_ALL"))
            for (size_t r = 0; r < pa->axf.size(); ++r)
            {
                const XF* pxf = pa->axf[r].get();
                const glm::vec3 p(pxf->matWorld[3]);
                std::fprintf(pf, "  record %zu ref %s kind %d world %.1f %.1f %.1f\n", r, name(pxf->pxfRef).c_str(), pxf->kind, p.x, p.y, p.z);
            }
        if (!paabind) continue;
        for (size_t i = 0; i < paabind->size(); ++i)
        {
            std::fprintf(pf, "  group %zu bones:", i);
            for (const GLOBBONEBIND& b : (*paabind)[i])
            {
                std::fprintf(pf, " %s", name(b.pxfBone).c_str());
                if (b.pxfBone && !mpbind.count(b.pxfBone) && !fHeld) { mpbind[b.pxfBone] = b.matBind; abone.push_back(b.pxfBone); }
            }
            std::fputc('\n', pf);
        }
    }
    // what it holds: its vertices in the world now, their main axis, where along it the hand is, and the far end's
    // bend (the hook), in the held object's own first record's space
    if (ploHeld && FCidDerivedFrom(ploHeld->Cid(), 0x30) && !static_cast<ALO*>(ploHeld)->axf.empty())
    {
        std::vector<glm::vec3> apos;
        for (ALO* pa : aalo)
        {
            if (!FUnderLo(pa, ploHeld) || !pa->pglobset) continue;
            const GLOBSET& gs = *pa->pglobset;
            const glm::mat4 matAlo = pa->axf.empty() ? glm::mat4(1.0f) : pa->axf[0]->matWorld;
            for (const GLOB& glob : gs.aglob)
            {
                const GLOB& geom = !glob.avtx.empty() || glob.iglobTemplate < 0 ? glob : gs.aglob[size_t(glob.iglobTemplate)];
                const XF* pxfO = glob.oidOwner >= 0 ? PxfFindAlo(pa, glob.oidOwner) : nullptr;
                glm::mat4 m = pxfO ? pxfO->matWorld : matAlo;
                if (glob.fMatrix) m = m * glob.dmat;
                for (const VTXGLOB& v : geom.avtx) apos.push_back(glm::vec3(m * glm::vec4(v.pos, 1.0f)));
            }
        }
        const glm::mat4 matRoot = static_cast<ALO*>(ploHeld)->axf[0]->matWorld;
        const glm::mat4 matInv = glm::inverse(matRoot);
        std::fprintf(pf, "\nheld: %zu vertices; its first record's world %.1f %.1f %.1f, attached to %s\n", apos.size(), matRoot[3].x, matRoot[3].y,
                     matRoot[3].z, name(static_cast<ALO*>(ploHeld)->axf[0]->pxfRef).c_str());
        if (apos.size() >= 3)
        {
            glm::vec3 c(0.0f);
            for (const glm::vec3& p : apos) c += p;
            c /= float(apos.size());
            glm::mat3 cov(0.0f);
            for (const glm::vec3& p : apos) cov += glm::outerProduct(p - c, p - c);
            glm::vec3 a = glm::normalize(glm::vec3(1.0f, 0.3f, 0.1f));
            for (int it = 0; it < 64; ++it) a = glm::normalize(cov * a);   // (the main axis: power iteration)
            float tMin = 1e9f, tMax = -1e9f;
            for (const glm::vec3& p : apos) tMin = std::min(tMin, glm::dot(p - c, a)), tMax = std::max(tMax, glm::dot(p - c, a));
            // the two ends' bends from the axis (the hook's end bends most)
            glm::vec3 aoff[2]{};
            int ac[2]{};
            for (const glm::vec3& p : apos)
            {
                const float t = glm::dot(p - c, a);
                const int e = t > tMax - 0.15f * (tMax - tMin) ? 1 : t < tMin + 0.15f * (tMax - tMin) ? 0 : -1;
                if (e < 0) continue;
                aoff[e] += (p - c) - t * a;
                ++ac[e];
            }
            for (int e = 0; e < 2; ++e) if (ac[e]) aoff[e] /= float(ac[e]);
            const glm::vec3 aL = glm::normalize(glm::vec3(matInv * glm::vec4(a, 0.0f)));
            const glm::vec3 cL(matInv * glm::vec4(c, 1.0f));
            std::fprintf(pf, "  axis (world) %.3f %.3f %.3f, length %.1f (t %.1f .. %.1f from the centre)\n", a.x, a.y, a.z, tMax - tMin, tMin, tMax);
            std::fprintf(pf, "  in its record's space: centre %.2f %.2f %.2f, axis %.3f %.3f %.3f\n", cL.x, cL.y, cL.z, aL.x, aL.y, aL.z);
            for (int e = 0; e < 2; ++e)
            {
                const glm::vec3 oL(matInv * glm::vec4(aoff[e], 0.0f));
                std::fprintf(pf, "  end %s (%d vertices): bend %.1f, in its record's space %.2f %.2f %.2f\n", e ? "t max" : "t min", ac[e],
                             glm::length(aoff[e]), oL.x, oL.y, oL.z);
            }
            if (SLY* psly = PslyGlobal(); psly && psly->plo19A4)
            {
                const glm::vec3 q = PosLoWorld(psly->plo19A4);
                const float t = glm::dot(q - c, a);
                const glm::vec3 qL(matInv * glm::vec4(q, 1.0f));
                std::fprintf(pf, "  the cane point (SLY +0x19A4, oid %d) at t %.1f, %.1f from the axis; in the record's space %.2f %.2f %.2f\n",
                             int(psly->plo19A4->oid), t, glm::length((q - c) - t * a), qL.x, qL.y, qL.z);
            }
            // the vertex counts along the axis, in tenths (the crook has the most)
            int ahist[10]{};
            for (const glm::vec3& p : apos) ++ahist[std::clamp(int((glm::dot(p - c, a) - tMin) / (tMax - tMin) * 10.0f), 0, 9)];
            std::fprintf(pf, "  vertices along it (t min .. t max, tenths):");
            for (int h : ahist) std::fprintf(pf, " %d", h);
            std::fprintf(pf, "\n");
            const XF* pxfHand = static_cast<ALO*>(ploHeld)->axf[0]->pxfRef;
            if (SLY* psly = PslyGlobal(); psly && psly->plo19A4 && pxfHand)
            {
                // vrbody's own measure (in the world: any space rigid with the cane will do)
                const glm::vec3 posPointW = PosLoWorld(psly->plo19A4), posHandW(pxfHand->matWorld[3]);
                CANEGEO g;
                MeasureCaneVertices(apos, posHandW, &posPointW, &g);
                if (g.fOk)
                {
                    const glm::vec3 o(g.matFrame[3]), zc(g.matFrame[2]), yc(g.matFrame[1]);
                    std::fprintf(pf, "  VR grip: the hand's record %.1f from the grip point; the hook point %.1f along -Z (%.1f off it); "
                                 "the plain end %.1f along +Z; the crook's side -Y: the hook point %.1f along -Y\n",
                                 glm::length(posHandW - o), glm::dot(posPointW - o, -zc), glm::length((posPointW - o) - glm::dot(posPointW - o, zc) * zc),
                                 [&] { float m = 0.0f; for (const glm::vec3& p : apos) m = std::max(m, glm::dot(p - o, zc)); return m; }(),
                                 glm::dot(posPointW - o, -yc));
                }
            }
            if (pxfHand)
            {
                const glm::vec3 h(pxfHand->matWorld[3]);
                const float t = glm::dot(h - c, a);
                std::fprintf(pf, "  the hand record %s at t %.1f, %.1f from the axis; the record's origin at t %.1f\n", name(pxfHand).c_str(), t,
                             glm::length((h - c) - t * a), glm::dot(glm::vec3(matRoot[3]) - c, a));
            }
        }
    }
    std::unordered_map<const XF*, int> mpidx;
    for (size_t i = 0; i < abone.size(); ++i) mpidx[abone[i]] = int(i);
    std::fprintf(pf, "\nbones (in the order the body binds them):\n");
    for (size_t i = 0; i < abone.size(); ++i)
    {
        const XF* pxf = abone[i];
        int c = 0;
        for (const XF* o : abone) c += o->pxfRef == pxf;
        const glm::vec3 w(pxf->matWorld[3]), b(glm::inverse(mpbind[pxf])[3]);
        auto jt = mpidx.find(pxf->pxfRef);
        std::fprintf(pf, "  %2zu = %-8s parent %-8s (bone %2d) depth %2d children %d  world %8.1f %8.1f %8.1f  bind %7.1f %7.1f %7.1f%s\n", i,
                     name(pxf).c_str(), name(pxf->pxfRef).c_str(), jt == mpidx.end() ? -1 : jt->second, Depth(pxf), c, w.x, w.y, w.z, b.x, b.y, b.z,
                     pxf == pxfSeed ? "  <- seed" : "");
    }
    // the hands the VR mod would pick (vrbody's FindHands, the default settings)
    VRHANDSCFG cfg;
    cfg.pxfSeedR = pxfSeed;
    const XF* pxfHeldAt = ploHeld && FCidDerivedFrom(ploHeld->Cid(), 0x30) && !static_cast<ALO*>(ploHeld)->axf.empty()
                        ? static_cast<ALO*>(ploHeld)->axf[0]->pxfRef : nullptr;
    if (!abone.empty())
    {
        FindHands(abone, mpbind, pxfHeldAt, palo, cfg);
        std::fprintf(pf, "VR hands: left %s, right %s (the right by %s); forearms %s, %s\n", name(s_hands.apxfHand[0]).c_str(),
                     name(s_hands.apxfHand[1]).c_str(), s_hands.szHow, name(s_hands.apxfArm[0]).c_str(), name(s_hands.apxfArm[1]).c_str());
    }
    std::fputc('\n', pf);
}
}  // namespace

void DumpVrSkeleton(SW* psw, const char* szPath)
{
    FILE* pf = std::fopen(szPath, "w");
    if (!pf || !psw) { if (pf) std::fclose(pf); return; }
    LO* ppo = PloPoCurrent(psw);
    std::fprintf(pf, "player oid %d\n", ppo ? int(ppo->oid) : -1);
    if (SLY* psly = PslyGlobal(); psly && psly->palo) DumpOneSkeleton(psw, psly->palo, psly->plo19A0, nullptr, "Sly", pf);
    for (const auto& up : psw->aploAll)
    {
        LO* plo = up.get();
        if (!FCidDerivedFrom(plo->Cid(), 0x30)) continue;
        if (plo->Cid() == 0x37 && PbentleyFromLo(plo))
            DumpOneSkeleton(psw, static_cast<ALO*>(plo), nullptr, PbentleyFromLo(plo)->pxf10B8, "Bentley", pf);
        else if (plo->Cid() == 0x36 && PmurrayFromLo(plo))
            DumpOneSkeleton(psw, static_cast<ALO*>(plo), nullptr, nullptr, "Murray", pf);
    }
    std::fclose(pf);
}
