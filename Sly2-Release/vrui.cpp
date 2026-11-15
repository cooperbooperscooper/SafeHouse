// (PC, VR) Messages over the VR HUD layer, and the wristwatch's face (vrui.h).
#include "vrui.h"
#include "gl.h"
#include "hud.h"
#include "screenobj.h"
#include "screenrt.h"

#include <algorithm>
#include <cmath>

namespace
{
// The wristwatch's face on the 640 x 448 canvas (DrawVrWatch, IWatchEntryAt): a plate, the title, a rule, then the
// entries: one column of up to 5, else two columns of up to 8 rows (16 entries at most), filled down, then across
constexpr float kxPlate0 = 14.0f, kxPlate1 = 626.0f, kyPlate0 = 10.0f, kyPlate1 = 438.0f;
constexpr float kyTitle = 24.0f, kyRule = 74.0f;
constexpr float kxRows0 = 34.0f, kxRows1 = 606.0f, kyRows0 = 88.0f, kyRows1 = 426.0f, kdxGap = 14.0f, khRowMax = 64.0f;
constexpr int kcOneColumn = 5, kcRowsMax = 8;
constexpr float kPixAspect = 480.0f / 448.0f;   // (a canvas pixel is 448 / 480 as wide as high: a width x this looks square)

struct WATCHGRID
{
    int c = 0, ccol = 1, crow = 1;
    float dxCol = 0.0f, hRow = 0.0f;
};
WATCHGRID GridOf(int c)
{
    WATCHGRID g;
    g.c = std::clamp(c, 0, 2 * kcRowsMax);
    g.ccol = g.c > kcOneColumn ? 2 : 1;
    g.crow = std::max(1, (g.c + g.ccol - 1) / g.ccol);
    g.dxCol = (kxRows1 - kxRows0 - kdxGap * float(g.ccol - 1)) / float(g.ccol);
    g.hRow = std::min(khRowMax, (kyRows1 - kyRows0) / float(g.crow));
    return g;
}
// Entry i's cell, canvas pixels (the lit plate's and the hit test's)
void CellOf(const WATCHGRID& g, int i, float* px0, float* py0, float* px1, float* py1)
{
    const int col = i / g.crow, row = i % g.crow;
    *px0 = kxRows0 + float(col) * (g.dxCol + kdxGap);
    *px1 = *px0 + g.dxCol;
    *py0 = kyRows0 + float(row) * g.hRow;
    *py1 = *py0 + g.hRow;
}

// A rectangle with rounded corners (r canvas pixels high, the width corrected for the pixel's aspect), in pieces that
// do not overlap (the HUD layer blends its coverage: an overlap would show)
void FillRounded(float x0, float y0, float x1, float y1, float r, glm::vec4 rgba, float z)
{
    const float rx = r * kPixAspect;
    g_hud.FillScreenRect(x0 + rx, y0, x1 - rx, y1, rgba, z);
    g_hud.FillScreenRect(x0, y0 + r, x0 + rx, y1 - r, rgba, z);
    g_hud.FillScreenRect(x1 - rx, y0 + r, x1, y1 - r, rgba, z);
    constexpr int cseg = 6;
    const glm::vec2 acentre[4] = {{x0 + rx, y0 + r}, {x1 - rx, y0 + r}, {x1 - rx, y1 - r}, {x0 + rx, y1 - r}};
    std::vector<glm::vec2> apos;
    apos.reserve(4 * cseg * 3);
    for (int k = 0; k < 4; ++k)
    {
        // (each corner's quarter, turned outward: top left 180..270 degrees with y down, top right 270..360, ...)
        const float a0 = glm::radians(180.0f + 90.0f * float(k)), da = glm::radians(90.0f) / float(cseg);
        for (int j = 0; j < cseg; ++j)
        {
            const float a = a0 + da * float(j), b = a + da;
            apos.push_back(acentre[k]);
            apos.push_back(acentre[k] + glm::vec2(std::cos(a) * rx, std::sin(a) * r));
            apos.push_back(acentre[k] + glm::vec2(std::cos(b) * rx, std::sin(b) * r));
        }
    }
    g_hud.FillScreenTris(apos.data(), int(apos.size()), rgba, z);
}
}  // namespace

void DrawVrMessage(const std::vector<std::string>& alines, float uProgress)
{
    const FONTBRX* pfont = PfontHud();
    if (!pfont || alines.empty()) return;
    const float size = 24.0f, hLine = size * 1.25f;
    auto dx = [&](const std::string& s) {
        float w = 0.0f;
        for (unsigned char ch : s) w += pfont->DxFromCh(ch);
        return w * size / 51.2f;   // (as skipcut.cpp's DxText: plain ASCII here)
    };
    float wMax = 0.0f;
    for (const std::string& s : alines) wMax = std::max(wMax, dx(s));
    const bool fBar = uProgress >= 0.0f;
    const float hBar = fBar ? 22.0f : 0.0f;
    const float h = hLine * float(alines.size());
    const float y0 = 224.0f - (h + hBar) * 0.5f;
    const float depth = 16000000.0f;   // (over every screen object, as the skip hint)
    g_hud.Begin();
    g_hud.FillScreenRect(320.0f - wMax * 0.5f - 18.0f, y0 - 14.0f, 320.0f + wMax * 0.5f + 18.0f, y0 + h + hBar + 10.0f,
                         glm::vec4(0, 0, 0, 104), depth / 16777216.0f - 1e-4f);
    if (fBar)
    {
        // the hold's bar: an empty track, filled in the title's gold as the triggers stay held
        const float x0 = 320.0f - wMax * 0.5f, x1 = 320.0f + wMax * 0.5f, yb = y0 + h + 6.0f;
        g_hud.FillScreenRect(x0, yb, x1, yb + 10.0f, glm::vec4(60, 60, 60, 128), depth / 16777216.0f - 5e-5f);
        if (uProgress > 0.0f)
            g_hud.FillScreenRect(x0, yb, x0 + (x1 - x0) * std::min(uProgress, 1.0f), yb + 10.0f, glm::vec4(230, 200, 90, 128),
                                 depth / 16777216.0f - 2e-5f);
    }
    for (size_t i = 0; i < alines.size(); ++i)
    {
        const float x = 320.0f - dx(alines[i]) * 0.5f, y = y0 + hLine * float(i);
        const glm::vec4 rgb = i == 0 ? glm::vec4(230.0f, 200.0f, 90.0f, 0.0f) : glm::vec4(210.0f, 210.0f, 210.0f, 0.0f);
        g_hud.DrawTextBox(*pfont, PfontHudIcons(), alines[i], x - 320.0f, (y - 224.0f) * 1.1f, size, 1.0f, 1.0f, 0.0f, 0.0f,
                          0, 0, rgb, glm::vec4(1.0f), glm::vec4(0.0f), depth + 100.0f, false);
    }
    g_hud.Flush(g_gl.renderWidth, g_gl.renderHeight);
}

void DrawVrWatch(const std::vector<std::string>& astr, int iCur, int iHover)
{
    const FONTBRX* pfont = PfontHud();
    if (!pfont) return;
    auto dx = [&](const std::string& s, float size) {
        float w = 0.0f;
        for (unsigned char ch : s) w += pfont->DxFromCh(ch);
        return w * size / 51.2f;   // (as DrawVrMessage)
    };
    const float depth = 16000000.0f;   // (over every screen object, as DrawVrMessage)
    const float z = depth / 16777216.0f;
    const glm::vec4 rgbGold(230.0f, 200.0f, 90.0f, 0.0f), rgbText(210.0f, 210.0f, 210.0f, 0.0f);
    const glm::vec4 rgbLit(250.0f, 250.0f, 250.0f, 0.0f), rgbDim(150.0f, 150.0f, 150.0f, 0.0f);
    auto text = [&](const std::string& s, float x, float y, float size, const glm::vec4& rgb) {
        g_hud.DrawTextBox(*pfont, PfontHudIcons(), s, x - 320.0f, (y - 224.0f) * 1.1f, size, 1.0f, 1.0f, 0.0f, 0.0f, 0, 0, rgb,
                          glm::vec4(1.0f), glm::vec4(0.0f), depth + 100.0f, false);
    };
    g_hud.Begin();
    // the case: a rim in the game's gold, dulled, round a dark face (opaque: the world does not show through)
    const glm::vec4 rgbaRim(120.0f, 100.0f, 48.0f, 128.0f);
    FillRounded(kxPlate0, kyPlate0, kxPlate1, kyPlate1, 30.0f, rgbaRim, z - 1e-4f);
    FillRounded(kxPlate0 + 5.0f * kPixAspect, kyPlate0 + 5.0f, kxPlate1 - 5.0f * kPixAspect, kyPlate1 - 5.0f, 25.0f,
                glm::vec4(16.0f, 18.0f, 26.0f, 128.0f), z - 8e-5f);
    const std::string strTitle = "GADGETS";
    text(strTitle, 320.0f - dx(strTitle, 30.0f) * 0.5f, kyTitle, 30.0f, rgbGold);
    g_hud.FillScreenRect(kxRows0, kyRule, kxRows1, kyRule + 2.0f, rgbaRim, z - 6e-5f);
    const WATCHGRID g = GridOf(int(astr.size()));
    if (!g.c)
    {
        const std::string str = "No gadgets yet";
        text(str, 320.0f - dx(str, 26.0f) * 0.5f, 0.5f * (kyRows0 + kyRows1) - 26.0f * 0.5f, 26.0f, rgbDim);
    }
    for (int i = 0; i < g.c; ++i)
    {
        float x0, y0, x1, y1;
        CellOf(g, i, &x0, &y0, &x1, &y1);
        const float yMid = 0.5f * (y0 + y1);
        // the one the left hand points at, on a lit plate
        if (i == iHover)
            FillRounded(x0, y0 + 2.0f, x1, y1 - 2.0f, std::min(10.0f, (y1 - y0) * 0.25f), glm::vec4(58.0f, 78.0f, 120.0f, 128.0f), z - 6e-5f);
        // the one in use (on the gadget button): the gold, and a small diamond before it
        if (i == iCur)
        {
            const float h = std::min(7.0f, (y1 - y0) * 0.16f), w = h * kPixAspect, xc = x0 + 12.0f;
            const glm::vec2 apos[6] = {{xc, yMid - h}, {xc + w, yMid}, {xc, yMid + h}, {xc, yMid - h}, {xc, yMid + h}, {xc - w, yMid}};
            g_hud.FillScreenTris(apos, 6, glm::vec4(230.0f, 200.0f, 90.0f, 128.0f), z - 4e-5f);
        }
        // (a long name made smaller to fit its cell)
        float size = std::clamp((y1 - y0) * 0.5f, 18.0f, 28.0f);
        const float xText = x0 + 26.0f, wMax = x1 - xText - 8.0f;
        if (const float w = dx(astr[i], size); w > wMax && w > 0.0f) size = std::max(12.0f, size * wMax / w);
        text(astr[i], xText, yMid - size * 0.5f, size, i == iCur ? rgbGold : i == iHover ? rgbLit : rgbText);
    }
    g_hud.Flush(g_gl.renderWidth, g_gl.renderHeight);
}

int IWatchEntryAt(float u, float v, int c)
{
    const WATCHGRID g = GridOf(c);
    const float x = u * 640.0f, y = v * 448.0f;
    for (int i = 0; i < g.c; ++i)
    {
        float x0, y0, x1, y1;
        CellOf(g, i, &x0, &y0, &x1, &y1);
        if (x >= x0 && x < x1 && y >= y0 && y < y1) return i;
    }
    return -1;
}
