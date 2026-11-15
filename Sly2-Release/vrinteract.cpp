// (PC, VR first person, 0.1.2) vrinteract.h: the physical interactions at step time.
#include "vrinteract.h"

#if SLY2N_VR
#include "vr.h"
#include "vrbody.h"

#include "actor.h"
#include "alo.h"
#include "brx.h"
#include "crv.h"
#include "disc.h"
#include "gadget.h"
#include "gang.h"
#include "joy.h"
#include "jt.h"
#include "lo.h"
#include "pccharacter.h"
#include "pcinput.h"
#include "pickup.h"
#include "po.h"
#include "pocket.h"
#include "shop.h"
#include "so.h"
#include "sound.h"
#include "splayer.h"
#include "sw.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace
{
// Reaches in game units (the world's own sizes, whatever your scale): a bare hand at a pocket's point, the drawn cane's
// hook there (its crook is smaller than a palm's reach), a hand (or the hook) at a climbable's real geometry.
constexpr float kSReachPocket = 40.0f;
constexpr float kSReachPocketHook = 35.0f;
constexpr float kSReachClimb = 45.0f;
// Hand over hand: the gripping hand's speed (m/s, in your own space) that is the stick pushed all the way.
constexpr float kMpsClimbFull = 0.9f;
// The steal's buzz: a strong one at once, a short weaker one after these steps (about 0.13 s at 60 a second).
constexpr int kCstepBuzz2 = 8;
// (pickpocket = hold) the steal's flick: the hand at the pocket moving this fast (m/s; a swing attack needs swing_speed,
// 2.5), the hook counting this many steps after it was there (a flick carries it away), the pause after a steal, and the
// soft beat in the hand while at the pocket in the stance
constexpr float kMpsStealFlick = 1.2f;
constexpr int kcstepHookRecent = 15, kcstepStealCool = 30, kcstepStanceBeat = 18;

struct STATE
{
    bool afZone[2]{};                 // the hands' grab zones as set on the last step (a press taken there too)
    int kBuzz2 = -1;                  // the steal's second buzz: its hand and the steps to it
    int cstepBuzz2 = 0;
    int cstepHookNear = 1000;         // (pickpocket = hold) steps since the hook was at the pocket, the steal's pause, the beat
    int cstepStealCool = 0, cstepStanceBeat = 0;
    ACTOR* paWatch = nullptr;         // the watch's entries as last sent: whose, the gadgets, their names, slot 0's
    std::vector<int> aidWatch;
    std::vector<std::string> astrWatch;
    int iCurWatch = -2;
};
STATE s;

int32_t NElf(uint32_t va) { return int32_t(g_disc.U32Elf(va)); }   // (an address comes back as the USA one: disc.h)

// The player: who, its own class's state (the jump is state 2 for all three), and which of the gadget menus is its
// (shop.cpp OpenGadgetMenu: 0 Sly, 1 Bentley, 2 Murray).
struct PLAYER
{
    ACTOR* pa = nullptr;
    SLY* psly = nullptr;
    BENTLEY* pbentley = nullptr;
    MURRAY* pmurray = nullptr;
    int ich = -1;
    int State() const { return psly ? psly->state : pbentley ? pbentley->state : pmurray ? pmurray->state : -1; }
};

bool FPlayerOf(SW* psw, PLAYER* pp)
{
    LO* plo = psw ? PloPoCurrent(psw) : nullptr;
    if (!plo || FPcCarm(plo)) return false;   // (PC, debug menu only: Carmelita's moveset has no pockets nor gadgets)
    PLAYER p;
    if ((p.psly = PslyFromLo(plo))) p.ich = 0;
    else if ((p.pbentley = PbentleyFromLo(plo))) p.ich = 1;
    else if ((p.pmurray = PmurrayFromLo(plo))) p.ich = 2;
    else return false;
    p.pa = p.psly ? static_cast<ACTOR*>(p.psly) : p.pbentley ? static_cast<ACTOR*>(p.pbentley) : static_cast<ACTOR*>(p.pmurray);
    if (!p.pa || p.pa->palo != plo) return false;
    *pp = p;
    return true;
}

void ClearZones()
{
    for (int k = 0; k < 2; ++k)
    {
        vr::SetGrabZone(k, false);
        s.afZone[k] = false;
    }
    vr::SetPocketNear(false);
    vr::SetPocketAvailable(false);
    vr::SetClimbStick(false, 0.0f, 0.0f);
}

// ---- pockets

// The pocket Circle would pick now and its point (target.cpp:89-90, sly.cpp:1295-1333, pocket.cpp:182-236 / 578-582):
// the guard Sly ranks 3 (his centre inside its pickpocket volume, in the world only while the pocket holds something
// and the guard is not alerted), Sly in a state where Circle pickpockets (0 / 1 / 10 / 12; not hypnotized, not in an
// attack mode's closed combo), prizes left; the point is where state 11's arm reaches: (0, 0, 25) in the readied
// prize's launch record (else the pocket object's own).
bool FPocketPoint(SLY* psly, LO** pploGuard, glm::vec3* ppos)
{
    if (!psly || psly->n10E4 != 3 || !psly->plo10DC) return false;
    const int st = psly->state;
    if (st != 0 && st != 1 && st != 10 && st != 12) return false;
    if (psly->f1D30 || (static_cast<unsigned>(psly->mode - 8) < 2 && !psly->f18E4)) return false;
    LO* ploGuard = psly->plo10DC;
    LO* ppocket = PloPocket(ploGuard);
    if (!ppocket || !FIsLoDerivedFrom(ppocket, 0x30)) return false;
    const POCKET& pk = PocketOf(ploGuard);
    if (pk.nMode < 1 || pk.i >= pk.c) return false;   // (-1 nothing readied, 0 alerted; 1 coins, 2 a prize)
    ALO* paloPocket = static_cast<ALO*>(ppocket);
    UpdateXfWorlds(paloPocket);   // (as CollectPocketDraws: the records' world matrices of this step)
    XF* pxf = PxfRewardPoint(ppocket);
    if (!pxf) pxf = PxfObj(paloPocket);
    if (!pxf) return false;
    *pploGuard = ploGuard;
    *ppos = glm::vec3(pxf->matWorld * glm::vec4(0.0f, 0.0f, 25.0f, 1.0f));
    return true;
}

// The take, at once (no reach, no prize pose: no prize card): the game's own bookkeeping (PloTakePocket: the task event
// 0x19, the reward launched (coins collect themselves), the glow off, the next prize), a pickup prize (loot 0x5E, a key
// 0x5C) collected now (launched with no collect time it would rest and expire uncounted); a strong buzz on the hand that
// took it, a short one after; the gadget menu's confirm as its sound (the take's own is in the skipped clip).
void Steal(LO* ploGuard, int k)
{
    LO* r = PloTakePocket(ploGuard);
    if (r && FIsLoDerivedFrom(r, 0x5A) && !FIsLoDerivedFrom(r, 0x5B) && !FIsLoDerivedFrom(r, 0x5D)) SetPickupState(r, 1);
    vr::Pulse(k, 0.8f, 0.08f);
    s.kBuzz2 = k;
    s.cstepBuzz2 = kCstepBuzz2;
    PsndStartSound(0x34);
}

// ---- climbing

// Where hand position q would hold candidate e (research climb.md 1a): a pipe's, rail's or ledge's curve at its nearest
// point, a spire's, hook's (the point itself, not the hang 150 below it) or swing's point, a round wall's rim at its
// height; the candidate's players' mask and height above it as the game's air search tests them (jumptgt.cpp). False:
// not a kind a grip grabs (the crawls, the uses, the jumps' own targets).
bool FClimbPoint(const ACTOR* pa, const JTENT& e, const glm::vec3& q, glm::vec3* ppos)
{
    if (!e.plo || !e.plo->fInWorld) return false;
    const JTDESC d = JtDescOf(e.plo, e.jtl);
    if (!(d.mask & static_cast<uint32_t>(pa->n1000))) return false;
    glm::vec3 pos(0.0f);
    switch (e.jtl)
    {
    case JTL_Pipe: case JTL_Rail: case JTL_Ledge:
    {
        const CRV* pcrv = PcrvJtPipe(e.plo);
        if (!pcrv || pcrv->apos.size() < 2) return false;
        NearestCrvAll(pcrv, q, &pos, nullptr, nullptr, nullptr);   // 0x1E6F60 (the world point)
        break;
    }
    case JTL_Spire: case JTL_Point: case JTL_Swing: pos = PosJtLo(e.plo); break;
    case JTL_Wall:
    {
        const glm::vec3 c = PosJtLo(e.plo);
        const uint32_t u = e.plo->U32Field(0, 0x10C, 0);
        float r;
        std::memcpy(&r, &u, sizeof(r));
        glm::vec3 dxy = q - c;
        dxy.z = 0.0f;
        const float l = glm::length(dxy);
        if (!(l > 1e-3f) || !(r > 0.0f)) return false;
        pos = c + dxy * (r / l);
        break;
    }
    default: return false;
    }
    if (d.height < PosAloCenter(pa->palo).z - pos.z) return false;
    *ppos = pos;
    return true;
}

// A Circle now would grab ploTarget the game's own way: in the air the best target in reach (ChooseJumpTarget; not in a
// flight to a target nor the glide, jump states 9..22, where Circle does nothing); on the ground the first tracked
// target within 250 (FInteractCircle), only when that is this one (KindJtCircle: no side effects), and never while Sly
// ranks a guard 3 (Circle would pickpocket).
bool FCircleGrabs(const PLAYER& p, LO* ploTarget)
{
    const int st = p.State();
    if (st == 2) return !(p.pa->jump.state >= 9 && p.pa->jump.state < 23);
    const bool fGround = p.psly       ? (st == 0 || st == 1 || st == 10 || st == 12)
                         : p.pbentley ? (st == 0 || st == 1 || st == 7 || st == 5)
                                      : (st == 0 || st == 1 || st == 10);
    if (!fGround) return false;
    if (p.psly && p.psly->n10E4 == 3) return false;
    LO* plo = nullptr;
    return KindJtCircle(p.pa, &plo) != -1 && plo == ploTarget;
}

// Holding on: an interaction this port ends on Circle's release (interact.cpp:932-941: kind 0 the ledges and walls, 6 the
// cane's reach for a hook or pipe, 13) or the flight to a target that may end in one (the jump states 9..16: spire,
// rail, ledge, wall, swing, hook, pipe, surface: Circle must be down on the arrival's step), where a held grip holds
// Circle (letting go lets go); and Sly on a pole or pipe (state 8), climbed hand over hand. The grips are grabs there
// (no buttons, no swings).
bool FHoldsCircle(const PLAYER& p)
{
    const int n = p.pa->n4C8;
    if (n == 0 || n == 6 || n == 13) return true;
    return p.State() == 2 && p.pa->jump.state >= 9 && p.pa->jump.state <= 16;
}
bool FOnPipe(const PLAYER& p) { return p.psly && p.psly->state == 8; }

// ---- the wristwatch

// The player's gadgets for the watch, in its gadget menu's order (shop.cpp SetGadgetMenu: the tables 0x2C2DE8 /
// 0x2C3028 / 0x2C3148 of 0x12 / 9 / 8 entries of 0x20 bytes, +0 the item, +0x1C its flags): owned, assignable (flag 1:
// the menu's message 0x33 otherwise) and usable on a button (the item table's use kind not -1).
void ListWatchGadgets(int ich, std::vector<int>* paid)
{
    static const uint32_t s_ava[3] = {0x2C2DE8, 0x2C3028, 0x2C3148};
    static const int s_ac[3] = {0x12, 9, 8};
    paid->clear();
    if (ich < 0 || ich > 2) return;
    for (int i = 0; i < s_ac[ich]; ++i)
    {
        const uint32_t v = s_ava[ich] + 0x20 * uint32_t(i);
        const int id = NElf(v);
        if (id < 0 || id >= 0x2B || !(uint32_t(NElf(v + 0x1C)) & 1u) || !FPowerupOwned(id)) continue;
        if (NElf(0x2BCD00 + 0x20 * uint32_t(id)) == -1) continue;
        paid->push_back(id);
    }
}

void UpdateWatch(SW* psw, const PLAYER& p)
{
    // the pick (the left trigger at an entry): onto the gadget button, the game's L1 slot, as the menu assigns it (only
    // one of this player's own: the entries may still be another's, just switched from)
    if (const int i = vr::TakeWatchPick(); i >= 0 && i < int(s.aidWatch.size()))
    {
        const int id = s.aidWatch[size_t(i)];
        std::vector<int> aidOwn;
        ListWatchGadgets(p.ich, &aidOwn);
        if (std::find(aidOwn.begin(), aidOwn.end(), id) != aidOwn.end())
        {
            if (IdActorAbilitySlot(p.pa, 0) != id) SetActorAbilitySlot(p.pa, 0, id);   // (0x141FC8: out of the other slots)
            PsndStartSound(0x34);   // (the menu's 0x1DAA90)
            vr::Pulse(0, 0.3f, 0.03f);
        }
    }
    // the entries: kept up while it is open (a gadget bought, slot 0 changed), and ready before a player's first look
    if (!vr::FWatchOpen() && s.paWatch == p.pa) return;
    std::vector<int> aid;
    ListWatchGadgets(p.ich, &aid);
    const int idCur = IdActorAbilitySlot(p.pa, 0);
    int iCur = -1;
    for (size_t i = 0; i < aid.size(); ++i)
        if (aid[i] == idCur) iCur = int(i);
    if (p.pa == s.paWatch && aid == s.aidWatch && iCur == s.iCurWatch) return;
    std::vector<std::string> astr;
    astr.reserve(aid.size());
    bool fNames = true;
    for (int id : aid)
    {
        std::string str = StrLocalized(psw, NElf(0x2BCD00 + 0x20 * uint32_t(id) + 0x14));   // (no IdText: the ELF's id)
        fNames = fNames && !str.empty();
        astr.push_back(str.empty() ? "#" + std::to_string(id) : std::move(str));   // (a name missing: still an entry)
    }
    vr::SetWatchEntries(astr, iCur);
    s.paWatch = fNames ? p.pa : nullptr;   // (a name missing, e.g. the world's text not in yet: asked again)
    s.aidWatch = std::move(aid);
    s.astrWatch = std::move(astr);
    s.iCurWatch = iCur;
}
}  // namespace

void VrStepInteract(SW* psw)
{
    if (s.cstepBuzz2 > 0 && --s.cstepBuzz2 == 0 && s.kBuzz2 >= 0) vr::Pulse(s.kBuzz2, 0.45f, 0.05f);
    // (only in first person at the player's head, a gang member playing, no menu, shop or safe house computer (as the
    // controllers' fMenu, joy.cpp), no binocucom, no hand calibration)
    PLAYER p;
    const LO* ploCur = psw ? PloPoCurrent(psw) : nullptr;
    const bool fMenu = FPcMenuMode() || FShopShown() || (ploCur && ploCur->Cid() == 0x46);
    if (!vr::Active() || !vr::FFirstPersonEyes() || fMenu || !FPlayerOf(psw, &p))
    {
        ClearZones();
        return;
    }
    const vrprofile::PROFILE& pr = vr::ProfileCur();
    // (the binocucom or the hand calibration: nothing grabbed, but a ledge held by a grip is kept held: the game lets
    // the binocucom open while hanging)
    if (vr::FHandCalibrating() || p.pa->nF04 != -1)
    {
        ClearZones();
        if (pr.fGrabClimb && (vr::FGripHeld(0) || vr::FGripHeld(1)) && FHoldsCircle(p)) vr::RequestButtons(BTN_Circle, 1);
        return;
    }

    // the hands (and the drawn cane's hook, in the right one) where the eyes last saw them
    bool afHand[2]{};
    glm::vec3 aposHand[2]{};
    for (int k = 0; k < 2; ++k)
    {
        glm::mat4 m(1.0f);
        afHand[k] = vr::FGripWorldLast(k, &m);
        aposHand[k] = glm::vec3(m[3]);
    }
    glm::vec3 posHook(0.0f);
    const bool fHook = p.psly && vr::FCaneDrawn() && FVrCaneHook(&posHook);

    // (a) pockets (pickpocket: 0 hold B, then the hook or a bare hand at the pocket flicked; 1 a grip at it, no B; 2 the
    // game's B only): each bare hand, or the hook in place of the right hand while the cane is drawn. With hold, B at a
    // pocket the game would pick is the stance (vr.cpp: no attacks while it is held); reach and steals only in it
    bool afPocket[2]{};
    LO* ploGuard = nullptr;
    glm::vec3 posPocket(0.0f);
    const bool fAvail = p.psly && pr.nSteal != 2 && FPocketPoint(p.psly, &ploGuard, &posPocket);
    vr::SetPocketAvailable(fAvail && pr.nSteal == 0);
    const bool fStance = pr.nSteal == 0 && vr::FStealStance();
    if (fAvail && (pr.nSteal == 1 || fStance))
        for (int k = 0; k < 2; ++k)
        {
            if (k == 1 && fHook) afPocket[k] = glm::distance(posHook, posPocket) < kSReachPocketHook;
            else afPocket[k] = afHand[k] && glm::distance(aposHand[k], posPocket) < kSReachPocket;
        }
    const bool fPocketNear = afPocket[0] || afPocket[1];
    // the hold's steal: in the stance, the hook or bare hand at the pocket flicked (the hook still counts a moment after
    // it was there); one steal, then a pause
    bool fStolen = false;
    if (s.cstepStealCool > 0) --s.cstepStealCool;
    s.cstepHookNear = afPocket[1] && fHook ? 0 : std::min(s.cstepHookNear + 1, 1000);
    if (fStance && fAvail && s.cstepStealCool == 0)
        for (int k = 0; k < 2 && !fStolen; ++k)
        {
            const bool fAt = k == 1 && fHook ? s.cstepHookNear <= kcstepHookRecent : afPocket[k];
            glm::vec3 velWorld(0.0f), velLocal(0.0f);
            if (!fAt || !vr::FGripVelocityLast(k, &velWorld, &velLocal) || glm::length(velLocal) < kMpsStealFlick) continue;
            Steal(ploGuard, k);
            fStolen = true;
            s.cstepStealCool = kcstepStealCool;
        }
    // (at the pocket in the stance: a soft beat in that hand, so the reach is felt before the flick)
    if (fStance && fPocketNear && !fStolen && ++s.cstepStanceBeat >= kcstepStanceBeat)
    {
        s.cstepStanceBeat = 0;
        vr::Pulse(afPocket[1] ? 1 : 0, 0.15f, 0.02f);
    }

    // (b) climbing: the hand's nearest tracked candidate it can grab now (the right hand's hook too), unless at a pocket
    // (Circle would steal there); while holding on, both hands hold
    LO* aploClimb[2]{};
    const bool fHold = pr.fGrabClimb && (FHoldsCircle(p) || FOnPipe(p));
    if (pr.fGrabClimb && !fHold && !fPocketNear)
        for (int k = 0; k < 2; ++k)
        {
            if (!afHand[k] && !(k == 1 && fHook)) continue;
            float dBest = kSReachClimb;
            for (int i = 0; i < g_jttrack.c && i < 4; ++i)
            {
                const JTENT& e = g_jttrack.ae[i];
                for (int j = 0; j < 2; ++j)
                {
                    if (j == 0 ? !afHand[k] : !(k == 1 && fHook)) continue;
                    const glm::vec3& q = j == 0 ? aposHand[k] : posHook;
                    glm::vec3 pos(0.0f);
                    if (!FClimbPoint(p.pa, e, q, &pos)) continue;
                    const float d = glm::distance(q, pos);
                    if (d < dBest && FCircleGrabs(p, e.plo)) dBest = d, aploClimb[k] = e.plo;
                }
            }
        }

    // the zones (vr.cpp: the grip grabs there, no swings, a tick on entering) and the presses taken in them (also in the
    // last step's, so a press made there is not left over)
    bool afZone[2]{};
    for (int k = 0; k < 2; ++k) afZone[k] = afPocket[k] || aploClimb[k] || fHold;
    for (int k = 0; k < 2; ++k)
    {
        if (!(afZone[k] || s.afZone[k]) || !vr::FTakeGripPress(k) || !afZone[k]) continue;
        if (afPocket[k])
        {
            if (!fStolen && s.cstepStealCool == 0) Steal(ploGuard, k), s.cstepStealCool = kcstepStealCool;
            fStolen = true;
        }
        else if (aploClimb[k])
        {
            vr::RequestButtons(BTN_Circle, 3);   // (a tap: the air search takes only the press's own step)
            vr::Pulse(k, 0.35f, 0.03f);
        }
    }
    for (int k = 0; k < 2; ++k)
    {
        vr::SetGrabZone(k, afZone[k]);
        s.afZone[k] = afZone[k];
    }
    vr::SetPocketNear(pr.nSteal == 1 && fPocketNear && !fStolen);   // (hold: the stance is what keeps the cane quiet)

    // holding on: Circle held while a grip is; on a pole, hand over hand: the gripping hand pulled one way moves you the
    // other, as the stick would (its right / up from your head's view, as the game reads the stick through its camera:
    // a sloping or level pipe too); a still or slow hand leaves the stick to you
    const bool fGrip = vr::FGripHeld(0) || vr::FGripHeld(1);
    if (pr.fGrabClimb && fGrip && FHoldsCircle(p)) vr::RequestButtons(BTN_Circle, 1);
    bool fStick = false;
    glm::vec2 vStick(0.0f);
    glm::vec3 posHead(0.0f);
    glm::mat3 matHead(1.0f);
    float tanX = 0.0f, tanY = 0.0f;
    uint64_t serial = 0;
    if (pr.fGrabClimb && FOnPipe(p) && vr::FHeadView(&posHead, &matHead, &tanX, &tanY, &serial))
    {
        glm::vec3 right = -matHead[1];
        right.z = 0.0f;
        right = glm::length(right) > 1e-3f ? glm::normalize(right) : glm::vec3(1.0f, 0.0f, 0.0f);
        const float upm = std::max(vr::UnitsPerMetre(), 1.0f);
        // (only the pull along the pipe counts, pushed along the pipe's direction on the screen as climb.cpp reads the
        // stick: its length the speed, its side of the pipe the way)
        glm::vec3 posPipe(0.0f), tanPipe(0.0f, 0.0f, 1.0f);
        if (p.pa->pcrvF50) EvalCrvS(p.pa->pcrvF50, p.pa->sCrvF50, &posPipe, &tanPipe);
        tanPipe = glm::length(tanPipe) > 1e-4f ? glm::normalize(tanPipe) : glm::vec3(0.0f, 0.0f, 1.0f);
        glm::vec2 dirPipe(glm::dot(tanPipe, right), tanPipe.z);
        dirPipe = glm::length(dirPipe) > 1e-3f ? glm::normalize(dirPipe) : glm::vec2(0.0f, 1.0f);
        for (int k = 0; k < 2; ++k)
        {
            glm::vec3 velWorld(0.0f), velLocal(0.0f);
            if (!vr::FGripHeld(k) || !vr::FGripVelocityLast(k, &velWorld, &velLocal)) continue;
            const glm::vec2 v = dirPipe * (-glm::dot(velWorld, tanPipe) / upm / kMpsClimbFull);
            if (glm::length(v) > glm::length(vStick)) vStick = v;
        }
        // (taken over from 0.35 only, and then past joy.cpp's 0.35 dead zone, so the game moves: a slow hand leaves the
        // stick to you)
        if (const float l = glm::length(vStick); l > 0.35f)
        {
            fStick = true;
            vStick *= (0.55f + 0.45f * std::min((l - 0.35f) / 0.65f, 1.0f)) / l;
        }
    }
    vr::SetClimbStick(fStick, vStick.x, vStick.y);

    // (c) the wristwatch, the gadget picker (gadget_menu = watch)
    if (pr.nGadgetMenu == 0) UpdateWatch(psw, p);
}
#else
void VrStepInteract(SW*) {}
#endif
