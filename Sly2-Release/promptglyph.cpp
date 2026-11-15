#include "actioninput.h"
// See promptglyph.h. After ThiefRush-Dev's Sly1-Release prompt_glyphs.cpp (PromptGlyphFor, PromptLabel); the
// keyboard keycaps are new (ThiefRush keeps the PlayStation glyphs for keyboard players).

#include "promptglyph.h"

#include "pcinput.h"
#include "shop.h"

#include "../platform/button_prompts.h"
#include "../platform/system_settings.h"

#include <GLFW/glfw3.h>
#if SLY2N_VR
#include "gl.h"
#include "joy.h"
#include "vr.h"
#include "vr_prompts_png.h"     // (generated: assets/prompts/prompts_vr.png)
#include "vrprompt_rects.inc"
#define STB_IMAGE_STATIC          // (a private copy, as qolview.cpp / texmod.cpp keep theirs)
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include <iterator>
#endif

#include <cctype>
#include <cstdint>

namespace
{
using cane::input::Button;
using cane::input::Family;

// The icon font's glyphs that name a game action.
struct GLYPHACTION { char ch; PCACTION action; };
constexpr GLYPHACTION s_aglyphaction[] = {
    {'X', PCA_Cross}, {'O', PCA_Circle}, {'S', PCA_Square}, {'T', PCA_Triangle},
    {'L', PCA_L1}, {'R', PCA_R1}, {'l', PCA_L2}, {'r', PCA_R2}, {'!', PCA_Start}, {'_', PCA_Select},
};

int ActionFromGlyph(char ch)
{
    for (const GLYPHACTION& g : s_aglyphaction)
        if (g.ch == ch) return g.action;
    return -1;
}

#if SLY2N_VR
// (PC, VR: vr.h) The VR controllers' icons (tools/pack_vr_prompts.py: Kenney's Meta Quest / Valve Index sets): a glyph's
// PS2 button, the physical control the playing character's profile puts it on (Sly2VR.ini), that control's icon.
unsigned TexVrPrompts()
{
    static unsigned s_tex = 0;
    static bool s_fTried = false;
    if (s_fTried) return s_tex;
    s_fTried = true;
    int dx = 0, dy = 0;
    stbi_uc* pb = stbi_load_from_memory(sly2n_vr_prompts_png, int(sizeof(sly2n_vr_prompts_png)), &dx, &dy, nullptr, 4);
    if (!pb) return 0;
    GLint texPrev = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texPrev);
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, dx, dy, 0, GL_RGBA, GL_UNSIGNED_BYTE, pb);   // (unflipped, as the atlases)
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, GLuint(texPrev));
    stbi_image_free(pb);
    s_tex = tex;
    return s_tex;
}

// The icon slot (vrprofile.h Ctl_*, 12 / 13 the left / right stick) a glyph is drawn with, -1: none (the usual icons)
int SlotVrGlyph(char ch)
{
    if (ch == '(' || ch == '+') return 12;   // (the left stick; the Touch has no d-pad)
    if (ch == ')') return 13;
    static constexpr struct { char ch; uint16_t btn; } s_a[] = {
        {'X', BTN_Cross}, {'O', BTN_Circle}, {'S', BTN_Square}, {'T', BTN_Triangle}, {'L', BTN_L1}, {'R', BTN_R1},
        {'l', BTN_L2}, {'r', BTN_R2}, {'!', BTN_Start}, {'_', BTN_Select}};
    for (const auto& g : s_a)
        if (g.ch == ch) return vr::CtlForButton(g.btn);
    return -1;
}

PROMPTGLYPH VrGlyph(char ch)
{
    PROMPTGLYPH glyph;
    if (!vr::FPromptsVr()) return glyph;
    const int slot = SlotVrGlyph(ch);
    if (slot < 0 || slot >= kVrPromptSlots) return glyph;
    const int* r = kVrPromptRects[vr::FIndexControllers() ? 1 : 0][slot];
    const unsigned tex = r[2] > 0 ? TexVrPrompts() : 0;
    if (!tex) return glyph;
    const float w = float(kVrPromptAtlasW), h = float(kVrPromptAtlasH);
    glyph.kind = PROMPTGLYPH::Icon;
    glyph.tex = tex;
    glyph.u0 = r[0] / w; glyph.v0 = (r[1] + r[3]) / h; glyph.u1 = (r[0] + r[2]) / w; glyph.v1 = r[1] / h;
    return glyph;
}

const char* PchzVrLabel(char ch)
{
    static constexpr const char* s_apsz[] = {"A", "B", "X", "Y", "Left trigger", "Right trigger", "Left grip",
                                             "Right grip", "Left stick click", "Right stick click", "Menu",
                                             "Right trackpad", "Left stick", "Right stick"};
    const int slot = vr::FPromptsVr() ? SlotVrGlyph(ch) : -1;
    if (slot < 0 || slot >= int(std::size(s_apsz))) return nullptr;
    if (vr::FIndexControllers() && (slot == 2 || slot == 3)) return slot == 2 ? "Left A" : "Left B";
    if (vr::FIndexControllers() && slot == 10) return "Left trackpad";
    return s_apsz[slot];
}
#endif

int s_textId = -1;
char s_chKeyOverride = '\0';
int s_keyOverride = 0, s_padOverride = -2;
std::optional<Family> s_familyOverride;
constexpr int s_idTurretBriefing = 1594;
constexpr int s_idTankBriefing = 2268;

int PromptBinding(char ch, int button, bool controller)
{
    if (!sly2actions::profile.enabled)
        return controller ? g_pcinput.gamepad[size_t(button)] : g_pcinput.keyboard[size_t(button)];

    // USA string IDs for prompts that describe a specific action even outside its live context.
    using sly2actions::Action;
    struct Cue { int textId; char glyph; Action action; };
    static constexpr Cue cues[] = {
        {211, 'R', Action::Paraglider}, {3237, 'R', Action::Paraglider},
        {647, 'R', Action::Photo},
        {754, 'O', Action::SlyPickpocket}, {1430, 'O', Action::SlyPickpocket},
        {753, 'R', Action::BentleyDart}, {807, 'R', Action::BentleyDart}, {1465, 'R', Action::BentleyDart},
        {340, 'S', Action::CarryThrow}, {344, 'S', Action::CarryThrow}, {804, 'S', Action::CarryThrow},
        {1398, 'S', Action::CarryThrow}, {1399, 'S', Action::CarryThrow},
        {3220, 'X', Action::VehicleDrive}, {3220, 'S', Action::VehiclePrimary},
        {s_idTurretBriefing, 'R', Action::TurretFire},
        {s_idTankBriefing, 'R', Action::VehiclePrimary},
        {900, 'X', Action::RcBomb}, {1091, 'X', Action::RcBomb}, {1091, 'S', Action::RcFire},
        {1676, 'X', Action::RcBomb}, {2563, 'X', Action::RcBomb}, {2563, 'S', Action::RcFire},
        {3248, 'X', Action::RcBomb}, {3249, 'X', Action::RcBomb}, {3249, 'S', Action::RcFire},
    };
    Action action = Action::Count;
    for (const Cue& cue : cues)
        if (cue.textId == s_textId && cue.glyph == ch) { action = cue.action; break; }
    const Action slot = cane::sly2input::legacyButtonAction(button);
    if (FGadgetMenuShown() && cane::sly2input::isGadgetSlot(slot)) action = slot;
    if (action == Action::Count) {
        if (FMouseMenuUp()) return controller ? g_pcinput.gamepad[size_t(button)] : g_pcinput.keyboard[size_t(button)];
        action = sly2actions::ForPromptButton(button);
    }
    return sly2actions::Binding(action, button, controller);
}

// The positional button an icon glyph means now (through the gamepad bindings), Button::Count when it is not a
// button glyph or the action is unbound. (The sticks and the d-pad are not remappable.)
Button ButtonForGlyph(char ch)
{
    if (ch == s_chKeyOverride && s_chKeyOverride && s_padOverride != -2) return cane::input::fromGlfwBinding(s_padOverride);
    switch (ch)
    {
    case '(': return Button::LS;
    case ')': return Button::RS;
    case '+': return Button::Dpad;
    default: break;
    }
    const int a = ActionFromGlyph(ch);
    if (a < 0) return Button::Count;
    return cane::input::fromGlfwBinding(PromptBinding(ch, a, true));
}


}  // namespace

PROMPTTEXTGLYPHS::PROMPTTEXTGLYPHS(int textId) : previous(s_textId) { s_textId = textId; }
PROMPTTEXTGLYPHS::~PROMPTTEXTGLYPHS() { s_textId = previous; }

PROMPTKEYGLYPH::PROMPTKEYGLYPH(char ch, int glfwKey, int controllerBinding, std::optional<Family> family)
    : chPrev(s_chKeyOverride), keyPrev(s_keyOverride), padPrev(s_padOverride), familyPrev(s_familyOverride)
{
    s_chKeyOverride = ch;
    s_keyOverride = glfwKey;
    s_padOverride = controllerBinding;
    s_familyOverride = family;
}

PROMPTKEYGLYPH::~PROMPTKEYGLYPH()
{
    s_chKeyOverride = chPrev;
    s_keyOverride = keyPrev;
    s_padOverride = padPrev;
    s_familyOverride = familyPrev;
}

std::string StrPromptButtonWords(const std::string& text, int textId)
{
    if (g_pcinput.fAlwaysOriginalPs) return text;
    const bool fireBriefing = sly2actions::profile.enabled &&
        (textId == s_idTurretBriefing || textId == s_idTankBriefing);
    if (text.find("START") == std::string::npos && text.find("SELECT") == std::string::npos &&
        !(fireBriefing && text.find("R1") != std::string::npos)) return text;
    struct WORD { const char* sz; size_t cch; const char* szIcon; bool briefingOnly = false; };
    static constexpr WORD s_aword[] = {{"START", 5, "&2!&."}, {"SELECT", 6, "&2_&."}, {"R1", 2, "&2R&.", true}};
    auto fLetter = [](unsigned char ch) { return std::isalnum(ch) || ch >= 0x80; };
    std::string out;
    out.reserve(text.size());
    bool fIconFont = false;   // (inside an "&2 ... &." run the words are glyph letters, not text)
    for (size_t i = 0; i < text.size();)
    {
        if (text[i] == '&' && i + 1 < text.size())
        {
            if (text[i + 1] != '&') fIconFont = text[i + 1] != '.';
            out.append(text, i, 2);
            i += 2;
            continue;
        }
        bool fDone = false;
        if (!fIconFont && (i == 0 || !fLetter(uint8_t(text[i - 1]))))
            for (const WORD& w : s_aword)
                // (after the word: not another capital or digit; a lower-case letter is a compound's next part, as the
                // Swedish "STARTknappen")
                if ((!w.briefingOnly || fireBriefing) && text.compare(i, w.cch, w.sz) == 0 &&
                    (i + w.cch == text.size() || !(std::isupper(uint8_t(text[i + w.cch])) || std::isdigit(uint8_t(text[i + w.cch])))))
                {
                    out += w.szIcon;
                    i += w.cch;
                    fDone = true;
                    break;
                }
        if (!fDone) out += text[i++];
    }
    return out;
}

Family PromptFamily()
{
    return cane::input::promptFamily(g_pcinput.promptStyle, g_pcinput.fAlwaysOriginalPs ? 1 : 0);
}

bool PromptKeyboardActive() { return cane::input::tracker().family() == Family::Keyboard; }

static PROMPTGLYPH PromptGlyphForButton(Button button, Family family)
{
    PROMPTGLYPH glyph;
    const auto& atlas = cane::input::promptAtlas();
    const auto uv = atlas.uv(family, button);
    const auto texture = atlas.texture(family);
    if (family != Family::PlayStation && texture && uv.valid) {
        glyph.kind = PROMPTGLYPH::Icon;
        glyph.tex = texture;
        glyph.u0 = uv.u0; glyph.v0 = uv.v0; glyph.u1 = uv.u1; glyph.v1 = uv.v1;
    } else {
        glyph.ch = cane::input::psGlyphFor(button);
        if (glyph.ch) glyph.kind = PROMPTGLYPH::Glyph;
    }
    return glyph;
}

PROMPTGLYPH PromptGlyphForBinding(int binding, Family family)
{
    if (family != Family::Keyboard) return PromptGlyphForButton(cane::input::fromGlfwBinding(binding), family);
    PROMPTGLYPH glyph;
    const auto& atlas = cane::input::promptAtlas();
    const auto uv = atlas.keyUV(cane::input::promptKeyForBinding(binding));
    if (atlas.keyboardTexture() && uv.valid) {
        glyph.kind = PROMPTGLYPH::Icon;
        glyph.tex = atlas.keyboardTexture();
        glyph.u0 = uv.u0; glyph.v0 = uv.v0; glyph.u1 = uv.u1; glyph.v1 = uv.v1;
    }
    return glyph;
}

PROMPTGLYPH PromptGlyphFor(char ch)
{
    PROMPTGLYPH original;
    original.ch = ch;
    const bool override = ch == s_chKeyOverride && s_chKeyOverride;
    if (override && s_familyOverride)
        return PromptGlyphForBinding(*s_familyOverride == Family::Keyboard ? s_keyOverride : s_padOverride, *s_familyOverride);
    if (g_pcinput.fAlwaysOriginalPs) return original;
#if SLY2N_VR
    if (const PROMPTGLYPH g = VrGlyph(ch); g.kind == PROMPTGLYPH::Icon) return g;   // (the VR controllers, used last)
#endif

    const Family family = PromptFamily();
    PROMPTGLYPH glyph;
    if (family == Family::Keyboard) {
        const int action = ActionFromGlyph(ch);
        if (action < 0) return original;
        int key = override ? s_keyOverride : PromptBinding(ch, action, false);
        if (action == PCA_Start && key == cane::settings::kKeyUnknown) key = GLFW_KEY_ESCAPE;
        glyph = PromptGlyphForBinding(key, family);
    } else {
        glyph = PromptGlyphForButton(ButtonForGlyph(ch), family);
    }
    return glyph.kind == PROMPTGLYPH::Original ? original : glyph;
}

std::string PromptLabelFor(char ch)
{
    const int a = ActionFromGlyph(ch);
    if (a < 0) return "";
#if SLY2N_VR
    if (!g_pcinput.fAlwaysOriginalPs)
        if (const char* pchz = PchzVrLabel(ch)) return pchz;
#endif
    if (PromptFamily() == Family::Keyboard && !g_pcinput.fAlwaysOriginalPs) {
        int key = ch == s_chKeyOverride && s_chKeyOverride ? s_keyOverride : PromptBinding(ch, a, false);
        if (a == PCA_Start && key == cane::settings::kKeyUnknown) key = GLFW_KEY_ESCAPE;
        return PchzKeyName(key);
    }
    if (g_pcinput.fAlwaysOriginalPs)
    {
        static const char* const s_apsz[PCA_Max] = {"Up", "Down", "Left", "Right", "Square", "Circle", "Cross", "Triangle",
                                                    "Start", "Select", "L1", "R1", "L2", "R2", "L3", "R3"};
        return s_apsz[a];
    }
    return cane::input::buttonLabel(ButtonForGlyph(ch), PromptFamily());
}

std::string StrPromptFallbackText(const std::string& text)
{
    if (!sly2actions::profile.enabled || g_pcinput.fAlwaysOriginalPs || text.find('&') == std::string::npos)
        return text;
    std::string result;
    result.reserve(text.size());
    char iconFont = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '&' && i + 1 < text.size()) {
            const char code = text[++i];
            if (code != '&') iconFont = code == '.' ? 0 : code;
            result += '&';
            result += code;
        } else if (iconFont && ActionFromGlyph(ch) >= 0 && PromptGlyphFor(ch).kind == PROMPTGLYPH::Original) {
            result += "&.[";
            result += PromptLabelFor(ch);
            result += "]&";
            result += iconFont;
        } else {
            result += ch;
        }
    }
    return result;
}
